"""MCP stdio entry point; reserve stdout for JSON-RPC."""
from pathlib import Path
from dotenv import load_dotenv
from mcp.server.fastmcp import FastMCP
from stock_service import StockService

load_dotenv(Path(__file__).with_name('.env'))
service = StockService()
mcp = FastMCP('美股行情验证')


@mcp.tool()
def get_stock_quote(symbol: str) -> dict:
    """查询美股/ETF报价。必须说明来源、成交时间、陈旧状态；is_demo时先说模拟数据。
    不支持纳斯达克指数点位，QQQ是ETF而非指数。错误时不得凭记忆编造价格。
    """
    return service.call('quote', symbol=symbol)


@mcp.tool()
def analyze_stock(symbol: str, days: int = 30) -> dict:
    """分析5至120根已结束日线的均线和区间收益。先报告时间/来源/模拟状态。
    不把历史走势当作预测；QQQ美元价格不能当成纳斯达克指数点位。
    """
    return service.call('analyze', symbol=symbol, days=days)


@mcp.tool()
def get_nasdaq100_overview(days: int = 30) -> dict:
    """分析纳斯达克100的QQQ ETF走势参考，包含报价和日线趋势。
    必须说明这是QQQ美元价格，不是指数点位，不是纳斯达克综合指数。
    is_demo时必须先声明模拟数据；逐项检查quote/trend的ok，失败不能补造结论。
    """
    return service.call('nasdaq100', days=days)


@mcp.tool()
def get_watchlist(symbols: list[str]) -> dict:
    """汇总最多10个美股或ETF报价；逐项保留错误，说明时间、IEX范围及模拟状态。"""
    return service.call('watchlist', symbols=symbols)


if __name__ == '__main__':
    mcp.run(transport='stdio')
