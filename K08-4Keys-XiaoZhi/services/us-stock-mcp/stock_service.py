"""Read-only Alpaca IEX prototype. Demo mode never represents market data."""
import math
import json
import os
import re
from datetime import datetime, timedelta, timezone
from statistics import mean
from zoneinfo import ZoneInfo

import httpx

UTC = timezone.utc
NY = ZoneInfo('America/New_York')
NOTICE = 'IEX 单一交易所数据，价格及成交量不代表全美市场；不要据此推断全市场资金流。'
DEMO = '模拟数据：仅用于验证功能，不是真实行情，不可用于投资分析。'


class DataError(Exception):
    pass


def timestamp(value):
    if not isinstance(value, str):
        raise DataError('行情缺少有效时间。')
    dt = datetime.fromisoformat(value.replace('Z', '+00:00'))
    if dt.tzinfo is None:
        raise DataError('行情时间缺少时区。')
    return dt.astimezone(UTC)


def number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value) if math.isfinite(value) else None


def symbol_name(value):
    if not isinstance(value, str):
        raise DataError('请输入美股或 ETF 代码。')
    value = value.strip().upper()
    if value in ('IXIC', '^IXIC', 'NDX', '^NDX', 'NASDAQ', '纳斯达克'):
        raise DataError('当前数据源不支持纳斯达克指数本身。QQQ 是纳指100 ETF，不能替代指数点位。')
    if not re.fullmatch(r'[A-Z][A-Z0-9.\-]{0,9}', value):
        raise DataError('无效股票代码；示例 QQQ、AAPL、NVDA。')
    return value


class StockService:
    def __init__(self, mode=None, client=None, now=None):
        self.mode = mode or os.getenv('STOCK_DATA_MODE', 'live')
        self.client = client or httpx.Client(timeout=8, follow_redirects=False)
        self.now = now or (lambda: datetime.now(UTC))

    def envelope(self):
        return {'source': 'synthetic-demo' if self.mode == 'demo' else 'Alpaca IEX',
                'is_demo': self.mode == 'demo', 'retrieved_at': self.now().isoformat(),
                'warnings': [DEMO] if self.mode == 'demo' else [NOTICE]}

    def call(self, action, **kwargs):
        result = self.envelope()
        try:
            if self.mode not in ('live', 'demo'):
                raise DataError('STOCK_DATA_MODE 只能为 live 或 demo。')
            handlers = {'quote': self.quote, 'analyze': self.analyze, 'watchlist': self.watchlist,
                        'nasdaq100': self.nasdaq100}
            data = handlers[action](**kwargs)
            json.dumps(data, allow_nan=False)
            result.update(ok=True, data=data)
        except (DataError, ValueError, TypeError, KeyError, AttributeError, OverflowError) as exc:
            # Provider payloads and exception strings may contain sensitive text.
            result.update(ok=False, error=str(exc) if isinstance(exc, DataError)
                          else '数据格式异常或参数无效，无法分析；不要补造行情。')
        return result

    def request(self, path, params):
        key, secret = os.getenv('APCA_API_KEY_ID'), os.getenv('APCA_API_SECRET_KEY')
        if not key or not secret:
            raise DataError('未配置 Alpaca API 凭据。请在本机 .env 配置；不会自动改用模拟数据。')
        try:
            response = self.client.get('https://data.alpaca.markets' + path,
                                       params={**params, 'feed': 'iex'},
                                       headers={'APCA-API-KEY-ID': key,
                                                'APCA-API-SECRET-KEY': secret})
            if response.status_code != 200:
                reasons = {401: 'API 凭据无效', 403: '无行情权限', 429: '查询过于频繁'}
                raise DataError(reasons.get(response.status_code, '行情服务暂不可用'))
            data = response.json()
            if not isinstance(data, dict):
                raise DataError('行情服务返回格式异常。')
            return data
        except httpx.RequestError:
            raise DataError('行情请求超时或网络不可用，请稍后重试。') from None

    def demo_bars(self, symbol):
        bases = {'QQQ': 400, 'SPY': 450, 'AAPL': 180, 'MSFT': 350, 'NVDA': 100}
        if symbol not in bases:
            raise DataError('演示仅支持 QQQ、SPY、AAPL、MSFT、NVDA。')
        rows = []
        start = datetime(2024, 1, 1, 5, tzinfo=UTC)
        for i in range(240):
            day = start + timedelta(days=i)
            if day.weekday() >= 5:
                continue
            price = bases[symbol] + len(rows) * .2 + math.sin(i) * 2
            rows.append({'t': day.isoformat(), 'c': round(price, 2), 'v': 10000 + i * 20})
        return rows

    def quote(self, symbol):
        symbol = symbol_name(symbol)
        if self.mode == 'demo':
            bars = self.demo_bars(symbol)
            snapshot = {'latestTrade': {'t': bars[-1]['t'], 'p': bars[-1]['c']},
                        'prevDailyBar': bars[-2]}
        else:
            snapshot = self.request('/v2/stocks/snapshots', {'symbols': symbol}).get(symbol)
        if not snapshot:
            raise DataError('该代码无行情数据。')
        trade = snapshot.get('latestTrade') or {}
        price = number(trade.get('p'))
        if price is None or price <= 0:
            raise DataError('未获取有效成交价。')
        at = timestamp(trade.get('t'))
        age = (self.now() - at).total_seconds()
        previous = snapshot.get('prevDailyBar') or {}
        prev_close = number(previous.get('c'))
        prev_at = timestamp(previous['t']) if previous.get('t') else None
        valid_previous = (prev_at is not None and prev_at.astimezone(NY).date() < at.astimezone(NY).date()
                          and prev_close is not None and prev_close > 0)
        return {'symbol': symbol, 'instrument_type': 'ETF' if symbol in ('QQQ', 'SPY') else 'equity',
                'currency': 'USD', 'price': price, 'trade_time': at.isoformat(),
                'trade_time_new_york': at.astimezone(NY).isoformat(),
                'age_seconds': round(age), 'stale_or_clock_error': age > 120 or age < -60,
                'previous_close': prev_close, 'previous_close_time': previous.get('t'),
                'change_percent': round((price / prev_close - 1) * 100, 4) if valid_previous else None,
                'note': ('QQQ 是跟踪纳斯达克100的 ETF；美元价格不是指数点位。' if symbol == 'QQQ'
                         else '涨跌幅以数据源上一个日线收盘为基准。'),
                'session_note': '仅提供成交时间，不推断当前是否开市；节假日及盘前盘后可能没有新成交。'}

    def analyze(self, symbol, days=30):
        symbol = symbol_name(symbol)
        if isinstance(days, bool) or not isinstance(days, int) or not 5 <= days <= 120:
            raise DataError('days 必须为 5–120 的整数，表示最多使用的已结束日线根数。')
        if self.mode == 'demo':
            bars = self.demo_bars(symbol)
        else:
            params = {'symbols': symbol, 'timeframe': '1Day', 'adjustment': 'all',
                      'start': (self.now() - timedelta(days=days * 2 + 40)).isoformat(),
                      'end': self.now().isoformat(), 'limit': 1000, 'sort': 'asc'}
            bars, seen = [], set()
            for _ in range(10):
                payload = self.request('/v2/stocks/bars', params)
                bars.extend(payload.get('bars', {}).get(symbol, []))
                token = payload.get('next_page_token')
                if not token:
                    break
                if token in seen:
                    raise DataError('行情分页异常，拒绝使用不完整数据。')
                seen.add(token)
                params['page_token'] = token
            else:
                raise DataError('数据超过分页上限，无法完整分析。')
        today = self.now().astimezone(NY).date()
        valid = {}
        for bar in bars:
            at = timestamp(bar.get('t'))
            close = number(bar.get('c'))
            if at.astimezone(NY).date() < today and close is not None and close > 0:
                valid[at] = close
        selected = sorted(valid.items())[-days:]
        if len(selected) < 2:
            raise DataError('已结束日线不足两根，无法分析。')
        closes = [p for _, p in selected]
        return {'symbol': symbol, 'currency': 'USD', 'requested_bars': days,
                'actual_bars': len(closes), 'first_bar': selected[0][0].isoformat(),
                'last_bar': selected[-1][0].isoformat(), 'last_close': closes[-1],
                'days_since_last_bar': (today - selected[-1][0].astimezone(NY).date()).days,
                'sample_shortfall': len(closes) < days,
                'return_percent': round((closes[-1] / closes[0] - 1) * 100, 4),
                'sma5': round(mean(closes[-5:]), 4) if len(closes) >= 5 else None,
                'sma20': round(mean(closes[-20:]), 4) if len(closes) >= 20 else None,
                'close_range': {'low': min(closes), 'high': max(closes)},
                'adjustment': 'synthetic' if self.mode == 'demo' else 'all',
                'note': '仅分析已结束日线，保守排除纽约当天。区间收益不是未来预测；收盘价范围不是盘中高低点。'
                        + (' QQQ 仅为纳指100走势参考，不是纳斯达克综合指数或指数点位。' if symbol == 'QQQ' else '')}

    def watchlist(self, symbols):
        if not isinstance(symbols, list) or not 1 <= len(symbols) <= 10:
            raise DataError('自选股需为 1–10 个代码的列表。')
        return [self.call('quote', symbol=s) for s in dict.fromkeys(symbol_name(x) for x in symbols)]

    def nasdaq100(self, days=30):
        quote = self.call('quote', symbol='QQQ')
        trend = self.call('analyze', symbol='QQQ', days=days)
        if not quote['ok'] and not trend['ok']:
            raise DataError(quote['error'])
        return {'benchmark': 'Nasdaq-100', 'index_quote_available': False,
                'proxy': 'QQQ', 'proxy_type': 'ETF',
                'partial': not (quote['ok'] and trend['ok']),
                'note': '纳斯达克100的 QQQ ETF 走势参考。不是指数点位，也不是纳斯达克综合指数。',
                'quote': quote, 'trend': trend}
