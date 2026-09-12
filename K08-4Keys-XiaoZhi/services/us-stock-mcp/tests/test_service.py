import json
import os
import sys
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest.mock import patch

import httpx

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from stock_service import StockService

NOW = datetime(2026, 9, 11, 15, tzinfo=timezone.utc)


class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.keys = patch.dict(os.environ, {'APCA_API_KEY_ID': 'test-key',
                                           'APCA_API_SECRET_KEY': 'test-secret'})
        self.keys.start()
        self.addCleanup(self.keys.stop)

    def service(self, handler=None, mode='live'):
        client = httpx.Client(transport=httpx.MockTransport(handler or (lambda r: None)))
        self.addCleanup(client.close)
        return StockService(mode=mode, client=client, now=lambda: NOW)

    def quote_handler(self, req):
        self.assertEqual(req.url.host, 'data.alpaca.markets')
        self.assertEqual(req.url.params['feed'], 'iex')
        self.assertEqual(req.method, 'GET')
        self.assertEqual(req.headers['APCA-API-KEY-ID'], 'test-key')
        return httpx.Response(200, json={'QQQ': {
            'latestTrade': {'p': 110, 't': '2026-09-11T14:00:00Z'},
            'prevDailyBar': {'c': 100, 't': '2026-09-10T04:00:00Z'}}})

    def test_quote_age_return_feed(self):
        result = self.service(self.quote_handler).call('quote', symbol='qqq')
        self.assertTrue(result['ok'])
        self.assertFalse(result['is_demo'])
        self.assertEqual(result['data']['change_percent'], 10)
        self.assertEqual(result['data']['age_seconds'], 3600)
        self.assertTrue(result['data']['stale_or_clock_error'])
        self.assertIn('不是指数点位', result['data']['note'])

    def test_missing_keys_never_falls_back(self):
        with patch.dict(os.environ, {}, clear=True):
            for action, args in [('quote', {'symbol': 'QQQ'}), ('nasdaq100', {})]:
                result = self.service().call(action, **args)
                self.assertFalse(result['ok'])
                self.assertFalse(result['is_demo'])
                self.assertIn('凭据', result['error'])

    def test_demo_explicit_historical(self):
        result = self.service(mode='demo').call('nasdaq100')
        self.assertTrue(result['is_demo'])
        self.assertIn('模拟', result['warnings'][0])
        self.assertFalse(result['data']['index_quote_available'])
        self.assertEqual(result['data']['proxy'], 'QQQ')
        self.assertTrue(result['data']['quote']['data']['trade_time'].startswith('2024'))
        json.dumps(result, allow_nan=False)

    def test_invalid_inputs(self):
        s = self.service(mode='demo')
        for symbol in ['^IXIC', 'NDX', 'NASDAQ', '../QQQ', '', None]:
            self.assertFalse(s.call('quote', symbol=symbol)['ok'])
        for days in [True, 0, 121, '30']:
            self.assertFalse(s.call('analyze', symbol='QQQ', days=days)['ok'])
        self.assertFalse(s.call('watchlist', symbols=['QQQ'] * 11)['ok'])
        self.assertFalse(s.call('quote', symbol='UNKNOWN')['ok'])

    def test_provider_errors_redacted(self):
        for status in [401, 403, 429, 500, 302]:
            s = self.service(lambda r: httpx.Response(status, text='test-secret'))
            result = s.call('quote', symbol='QQQ')
            self.assertFalse(result['ok'])
            self.assertNotIn('test-secret', json.dumps(result))

    def test_bad_payloads(self):
        for payload in [[], {}, {'QQQ': {'latestTrade': {'p': float('inf')}}},
                        {'QQQ': {'latestTrade': {'p': 12, 't': 'yesterday'}}}]:
            s = self.service(lambda r: httpx.Response(200, content=json.dumps(payload)))
            result = s.call('quote', symbol='QQQ')
            self.assertFalse(result['ok'])
            json.dumps(result, allow_nan=False)

    def test_network_timeout(self):
        def handler(req):
            raise httpx.ReadTimeout('secret in URL', request=req)
        result = self.service(handler).call('quote', symbol='QQQ')
        self.assertFalse(result['ok'])
        self.assertNotIn('secret', result['error'])

    def test_nonfinite_computed_metric_rejected(self):
        s = self.service(lambda r: httpx.Response(200, json={'QQQ': {
            'latestTrade': {'p': 1e308, 't': '2026-09-11T14:00:00Z'},
            'prevDailyBar': {'c': 1e-308, 't': '2026-09-10T04:00:00Z'}}}))
        result = s.call('quote', symbol='QQQ')
        self.assertFalse(result['ok'])
        json.dumps(result, allow_nan=False)

    def test_missing_baseline_is_not_zero_change(self):
        s = self.service(lambda r: httpx.Response(200, json={'QQQ': {
            'latestTrade': {'p': 110, 't': '2026-09-11T14:00:00Z'}}}))
        result = s.call('quote', symbol='QQQ')
        self.assertTrue(result['ok'])
        self.assertIsNone(result['data']['change_percent'])

    def test_future_timestamp_flagged(self):
        s = self.service(lambda r: httpx.Response(200, json={'QQQ': {
            'latestTrade': {'p': 110, 't': '2026-09-12T14:00:00Z'}}}))
        result = s.call('quote', symbol='QQQ')
        self.assertTrue(result['data']['stale_or_clock_error'])

    def test_pagination_adjustments_and_exclude_today(self):
        calls = []
        def handler(req):
            calls.append(req)
            self.assertEqual(req.url.params['adjustment'], 'all')
            if len(calls) == 1:
                return httpx.Response(200, json={'bars': {'QQQ': [
                    {'t': '2026-09-08T04:00:00Z', 'c': 100}]}, 'next_page_token': 'page2'})
            self.assertEqual(req.url.params['page_token'], 'page2')
            return httpx.Response(200, json={'bars': {'QQQ': [
                {'t': '2026-09-09T04:00:00Z', 'c': 110},
                {'t': '2026-09-10T04:00:00Z', 'c': 120},
                {'t': '2026-09-11T04:00:00Z', 'c': 999}]}})
        result = self.service(handler).call('analyze', symbol='QQQ', days=30)
        self.assertTrue(result['ok'])
        data = result['data']
        self.assertEqual(data['actual_bars'], 3)
        self.assertEqual(data['last_close'], 120)
        self.assertEqual(data['return_percent'], 20)
        self.assertIsNone(data['sma5'])
        self.assertTrue(data['sample_shortfall'])

    def test_repeated_pagination_rejected(self):
        s = self.service(lambda r: httpx.Response(200, json={
            'bars': {'QQQ': []}, 'next_page_token': 'repeat'}))
        self.assertFalse(s.call('analyze', symbol='QQQ')['ok'])

    def test_moving_averages(self):
        bars = [{'t': (NOW - timedelta(days=40 - i)).isoformat(), 'c': i + 1} for i in range(30)]
        s = self.service(lambda r: httpx.Response(200, json={'bars': {'QQQ': bars}}))
        data = s.call('analyze', symbol='QQQ')['data']
        self.assertEqual(data['sma5'], 28)
        self.assertEqual(data['sma20'], 20.5)

    def test_watchlist_preserves_item_failure(self):
        result = self.service(mode='demo').call('watchlist', symbols=['QQQ', 'UNKNOWN', 'QQQ'])
        self.assertEqual(len(result['data']), 2)
        self.assertTrue(result['data'][0]['ok'])
        self.assertFalse(result['data'][1]['ok'])


if __name__ == '__main__':
    unittest.main()
