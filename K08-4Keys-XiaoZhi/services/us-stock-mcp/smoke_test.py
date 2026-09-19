"""Exercise the real MCP stdio protocol locally, using explicit synthetic data."""
import asyncio
import os
from pathlib import Path
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main():
    params = StdioServerParameters(command=sys.executable,
                                  args=[str(Path(__file__).with_name('server.py'))],
                                  env={**os.environ, 'STOCK_DATA_MODE': 'demo'})
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            assert {t.name for t in tools.tools} == {
                'get_stock_quote', 'analyze_stock', 'get_watchlist', 'get_nasdaq100_overview'}
            cases = [('get_stock_quote', {'symbol': 'QQQ'}),
                     ('analyze_stock', {'symbol': 'QQQ', 'days': 30}),
                     ('get_watchlist', {'symbols': ['QQQ', 'NVDA']}),
                     ('get_nasdaq100_overview', {'days': 30})]
            for name, arguments in cases:
                response = await session.call_tool(name, arguments)
                assert not response.isError, name
                import json
                payload = response.structuredContent or json.loads(response.content[0].text)
                assert payload['is_demo'] and payload['ok'], payload
                print(f'PASS {name}: 模拟数据，非实时行情')
    print('PASS MCP initialize/list_tools/call_tool；未连接 xiaozhi.me 或行情 API。')


if __name__ == '__main__':
    asyncio.run(main())
