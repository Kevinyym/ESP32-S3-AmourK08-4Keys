# 小智美股与纳斯达克 100 走势工具

这个独立 MCP 服务向小智智能体提供只读的美股/ETF 行情工具。首版“纳斯达克 100 走势”使用 QQQ ETF 作为参考：QQQ 以美元计价，它的价格不是 Nasdaq-100 指数点位，也不是纳斯达克综合指数（IXIC）。当前数据源不能直接提供 NDX 或 IXIC 指数点位。

## 工具

- `get_nasdaq100_overview(days=30)`：主要语音入口，同时返回 QQQ 最新成交参考与 5–120 根已结束日线趋势。
- `get_stock_quote(symbol)`：查询一只美股或 ETF 的最新成交参考、时间与陈旧标志。
- `analyze_stock(symbol, days=30)`：计算已结束日线的区间收益、5/20 日均线和收盘价范围。
- `get_watchlist(symbols)`：汇总 1–10 个代码，并逐项保留失败结果。

可以对小智说：“看看纳斯达克 100 最近 30 个交易日的走势。”回答应先说明数据来源和时间；若 `is_demo=true`，必须先说明是模拟数据。任何错误都不应靠记忆补造价格或结论。

## 安装

需要 Python 3.11 或更新版本。在本目录执行。`requirements.lock.txt` 是已经完成本地验证的完整依赖版本；需要重新解析兼容版本时才改用 `requirements.txt`：

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.lock.txt
cp .env.example .env
```

`.env` 已被 `.gitignore` 忽略。所有密钥只保存在本机，不要把 API 密钥或带 token 的 `MCP_ENDPOINT` 发到聊天、日志或提交到 Git。

## 先在本机验证模拟数据

本地烟雾测试通过标准 MCP 客户端与 `server.py` 的 stdio 协议完成四个工具的往返，不需要 Alpaca 凭据，也不会连接 xiaozhi.me：

```sh
source .venv/bin/activate
python smoke_test.py
```

也可以让其他支持 stdio 的 MCP 客户端以当前 Python 解释器启动 `server.py`，并把 `STOCK_DATA_MODE` 显式设为 `demo`。模拟模式生成固定的合成历史数字，只用于检查工具和协议；它不是真实、延迟或预测行情，不能用于投资分析。

## 先在小智设备验证模拟数据

1. 在 [xiaozhi.me](https://xiaozhi.me/) 的智能体页面取得私密 MCP 接入点，只把完整的 `wss://...` 地址写入本机 `.env` 的 `MCP_ENDPOINT`。
2. 暂时把 `STOCK_DATA_MODE` 显式改为 `demo`，不要填写 Alpaca 凭据，启动桥接：

```sh
source .venv/bin/activate
python bridge.py
```

3. 在小智智能体中确认能看到工具，然后对设备说：“看看纳斯达克 100 最近 30 个交易日的走势。”此时回答必须明确说明是模拟数据，数字不能用于投资分析。

启动日志只能说明本机桥接正在尝试连接；必须在小智智能体中实际看到并调用工具，才能判定云端接入成功。

## 切换到真实 Alpaca IEX 数据

1. 在 [Alpaca](https://alpaca.markets/) 注册并从后台创建 Market Data API 凭据，把 `APCA_API_KEY_ID` 和 `APCA_API_SECRET_KEY` 写入本机 `.env`。
2. 把 `.env` 中的 `STOCK_DATA_MODE` 改为 `live`，重新启动 `python bridge.py`，再检查工具返回的 `source`、`is_demo` 和时间。

`live` 是默认模式。凭据缺失或无效时，工具会清楚报错，绝不会自动退回模拟数据。桥接进程通过当前 Python 解释器启动 `server.py`；连接中断后按 5–60 秒上限退避重连。它必须在一台持续运行且可以访问互联网的电脑或服务器上运行。接入服务本身不需要重新编译或烧录 ESP32 固件。

## 数据边界

- 实时和历史请求固定使用 Alpaca 的 `data.alpaca.markets` 主机、只读 GET 请求及 `feed=iex`。IEX 是单一交易所，不是覆盖全美交易所的 SIP；IEX 价格与成交量不代表全美市场，尤其不能用成交量推断全市场资金流。
- 休市、节假日、盘前盘后或该证券在 IEX 没有新成交时，最新成交可能为空或陈旧。工具返回成交时间与陈旧标志，不自行猜测是否开市。缺少有效时间或价格时会报错。
- 趋势只使用已结束的日线，区间收益与均线是历史描述，不是未来预测。QQQ 会尽量跟踪 Nasdaq-100，但 ETF 市场价格仍会受供求、买卖价差以及相对净值的溢价/折价影响。
- `STOCK_DATA_MODE=demo` 的数值完全是合成数据。使用真实行情必须显式设置 `live` 并提供本机凭据。

## 本次验证记录

2026-09-12 使用 Python 3.12.4 与 `requirements.lock.txt` 中的依赖完成验证：

- `python -m unittest discover -s tests -v`：19 项通过，覆盖分页、日线排除当天、均线、陈旧/未来时间、缺凭据、限流、坏数据、日志保护和子进程回收。
- `python smoke_test.py`：真实 MCP stdio 初始化、工具发现和四个工具调用通过，全部使用明确标识的合成数据。
- 桥接集成测试使用模拟 WebSocket 与真实 `server.py` 子进程；尚未连接 xiaozhi.me，也未使用 Alpaca 真实行情凭据。
- 现有 ESP32 固件未修改，无需重新烧录。

## 官方资料

- [小智官方 MCP 示例与 `mcp_pipe.py`](https://github.com/78/mcp-calculator)
- [Alpaca Snapshot API](https://docs.alpaca.markets/us/reference/stocksnapshots-1)
- [Alpaca Historical Bars API](https://docs.alpaca.markets/us/reference/stockbars)
- [Alpaca Market Data FAQ：IEX 与 SIP 的区别](https://docs.alpaca.markets/us/docs/market-data-faq)
- [Invesco QQQ 官方说明](https://www.invesco.com/qqq-etf/en/home.html)
