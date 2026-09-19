"""Bridge XiaoZhi's WebSocket MCP endpoint to the local stdio server."""

import asyncio
import json
import logging
import os
from pathlib import Path
import re
import sys
import ssl
import socket

import certifi
from urllib.parse import urlsplit

from dotenv import load_dotenv
from websockets.asyncio.client import connect


BASE_DIR = Path(__file__).resolve().parent
SERVER = BASE_DIR / "server.py"
MAX_MESSAGE_SIZE = 1024 * 1024
INITIAL_BACKOFF = 5
MAX_BACKOFF = 60

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
    stream=sys.stderr,
)
LOGGER = logging.getLogger("xiaozhi-stock-bridge")
REQUEST_LOG_PATTERN = re.compile(
    r"^Processing request of type ([A-Za-z][A-Za-z0-9_]*)$"
)


class BridgeSessionEnded(RuntimeError):
    """A connection or local server ended and should be restarted."""


def create_tls_context() -> ssl.SSLContext:
    """Keep system trust and add the packaged public CA roots on macOS."""
    context = ssl.create_default_context()
    context.load_verify_locations(cafile=certifi.where())
    return context


def connection_error_summary(error: Exception) -> str:
    """Only return fixed descriptions and numeric codes, never remote text."""
    if isinstance(error, ssl.SSLCertVerificationError):
        return "TLS 证书校验失败，请检查 CA 证书或网络代理证书"
    if isinstance(error, ssl.SSLError):
        return "TLS 握手失败"
    if isinstance(error, socket.gaierror):
        return "DNS 解析失败"
    if isinstance(error, TimeoutError):
        return "连接或响应超时"
    response = getattr(error, "response", None)
    status = getattr(response, "status_code", None)
    if type(status) is int and 100 <= status <= 599:
        return f"WebSocket 握手被拒绝，HTTP 状态码 {status}"
    if isinstance(error, OSError):
        return "网络连接或本地进程错误"
    return "连接或 MCP 会话异常（详细原文已隐藏）"


def validate_endpoint(endpoint: str | None) -> str:
    """Return a valid production endpoint without ever logging its value."""
    if not endpoint:
        raise ValueError("缺少 MCP_ENDPOINT；请在本机 .env 中配置小智 MCP 接入点。")
    parsed = urlsplit(endpoint)
    if parsed.scheme.lower() != "wss" or not parsed.hostname:
        raise ValueError("MCP_ENDPOINT 必须是有效的 wss:// 加密 WebSocket 地址。")
    if parsed.username or parsed.password:
        raise ValueError("MCP_ENDPOINT 不应在主机部分包含用户名或密码。")
    return endpoint


def jsonrpc_line(raw: bytes | str) -> str:
    """Validate and normalize one UTF-8 JSON-RPC message for stdio/WebSocket."""
    if isinstance(raw, bytes):
        if len(raw) > MAX_MESSAGE_SIZE:
            raise BridgeSessionEnded("message too large")
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            raise BridgeSessionEnded("invalid UTF-8") from None
    else:
        if len(raw.encode("utf-8")) > MAX_MESSAGE_SIZE:
            raise BridgeSessionEnded("message too large")
        text = raw
    try:
        value = json.loads(text)
    except json.JSONDecodeError:
        raise BridgeSessionEnded("invalid JSON") from None
    if not isinstance(value, dict) or value.get("jsonrpc") != "2.0":
        raise BridgeSessionEnded("invalid JSON-RPC")
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n"


async def start_server():
    """Start server.py with the current interpreter and private stdio pipes."""
    child_env = os.environ.copy()
    child_env.update(
        {
            "PYTHONIOENCODING": "utf-8",
            "PYTHONUTF8": "1",
            "PYTHONUNBUFFERED": "1",
            "HTTPX_LOG_LEVEL": "WARNING",
        }
    )
    return await asyncio.create_subprocess_exec(
        sys.executable,
        "-u",
        str(SERVER),
        cwd=str(BASE_DIR),
        env=child_env,
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        limit=MAX_MESSAGE_SIZE + 1,
    )


KNOWN_STOCK_TOOLS = frozenset({
    "get_stock_quote", "analyze_stock", "get_watchlist", "get_nasdaq100_overview",
})


def log_tool_request(line: str) -> None:
    """Log dispatch metadata only; never log arguments or arbitrary tool names."""
    request = json.loads(line)
    if request.get("method") != "tools/call":
        return
    params = request.get("params")
    name = params.get("name") if isinstance(params, dict) else None
    if isinstance(name, str) and name in KNOWN_STOCK_TOOLS:
        LOGGER.info("收到行情工具调用：%s", name)
    else:
        LOGGER.warning("收到未注册工具名的调用（名称和参数已隐藏）；请核对后台工具映射。")


async def websocket_to_stdio(websocket, process) -> None:
    assert process.stdin is not None
    async for message in websocket:
        line = jsonrpc_line(message)
        log_tool_request(line)
        process.stdin.write(line.encode("utf-8"))
        await process.stdin.drain()
    raise BridgeSessionEnded("WebSocket ended")


async def stdio_to_websocket(process, websocket) -> None:
    assert process.stdout is not None
    while True:
        raw = await process.stdout.readline()
        if not raw:
            raise BridgeSessionEnded("server stdout ended")
        await websocket.send(jsonrpc_line(raw))


async def stderr_to_log(process) -> None:
    assert process.stderr is not None
    hidden_notice_emitted = False
    while True:
        raw = await process.stderr.readline()
        if not raw:
            return
        line = raw.decode("utf-8", errors="replace").rstrip()
        request = REQUEST_LOG_PATTERN.fullmatch(line)
        if request:
            LOGGER.info("本地 MCP 正在处理请求：%s", request.group(1))
        elif line and not hidden_notice_emitted:
            LOGGER.warning("本地 MCP 写入了诊断日志；原文已隐藏。")
            hidden_notice_emitted = True


async def stop_server(process) -> None:
    """Close stdin and ensure the local subprocess cannot survive a session."""
    if process.stdin is not None:
        process.stdin.close()
        try:
            await asyncio.wait_for(process.stdin.wait_closed(), timeout=2)
        except (asyncio.TimeoutError, BrokenPipeError, ConnectionResetError):
            pass
    if process.returncode is not None:
        await process.wait()
        return
    try:
        process.terminate()
    except ProcessLookupError:
        await process.wait()
        return
    try:
        await asyncio.wait_for(process.wait(), timeout=5)
    except asyncio.TimeoutError:
        try:
            process.kill()
        except ProcessLookupError:
            pass
        await process.wait()


async def bridge_once(endpoint: str) -> None:
    """Run one connection. This boundary can be patched in local tests."""
    LOGGER.info("正在连接小智 MCP WebSocket。")
    process = None
    tasks: set[asyncio.Task] = set()
    try:
        async with connect(
            endpoint,
            ssl=create_tls_context(),
            max_size=MAX_MESSAGE_SIZE,
            ping_interval=30,
            ping_timeout=20,
        ) as websocket:
            LOGGER.info("小智 MCP WebSocket 已连接，正在启动本地行情服务。")
            process = await start_server()
            tasks = {
                asyncio.create_task(websocket_to_stdio(websocket, process)),
                asyncio.create_task(stdio_to_websocket(process, websocket)),
                asyncio.create_task(stderr_to_log(process)),
            }
            done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in pending:
                task.cancel()
            await asyncio.gather(*pending, return_exceptions=True)
            for task in done:
                try:
                    task.result()
                except asyncio.CancelledError:
                    raise
                except Exception:
                    raise BridgeSessionEnded("bridge task ended") from None
            raise BridgeSessionEnded("bridge task ended")
    finally:
        for task in tasks:
            if not task.done():
                task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
        if process is not None:
            await stop_server(process)
            LOGGER.info("本地行情服务已停止。")


async def run_forever(endpoint: str) -> None:
    """Reconnect forever with a bounded 5–60 second exponential backoff."""
    backoff = INITIAL_BACKOFF
    while True:
        started_at = asyncio.get_running_loop().time()
        try:
            await bridge_once(endpoint)
        except asyncio.CancelledError:
            raise
        except Exception as error:
            if asyncio.get_running_loop().time() - started_at >= 60:
                backoff = INITIAL_BACKOFF
            LOGGER.warning("%s；%d 秒后重连。", connection_error_summary(error), backoff)
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, MAX_BACKOFF)


def main() -> int:
    load_dotenv(BASE_DIR / ".env", override=False)
    try:
        endpoint = validate_endpoint(os.getenv("MCP_ENDPOINT"))
    except ValueError as error:
        LOGGER.error("%s", error)
        return 2
    try:
        asyncio.run(run_forever(endpoint))
    except KeyboardInterrupt:
        LOGGER.info("收到退出信号，桥接已停止。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
