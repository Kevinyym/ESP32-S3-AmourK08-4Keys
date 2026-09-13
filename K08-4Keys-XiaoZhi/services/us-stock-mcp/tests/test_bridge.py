import asyncio
from contextlib import asynccontextmanager
import json
import os
import sys
import ssl
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bridge


class BridgeTests(unittest.TestCase):
    def test_tls_context_keeps_verification_enabled(self):
        context = bridge.create_tls_context()
        self.assertTrue(context.check_hostname)
        self.assertEqual(context.verify_mode, ssl.CERT_REQUIRED)
        self.assertGreater(context.cert_store_stats()['x509_ca'], 0)

    def test_connection_errors_never_expose_remote_text(self):
        secret = 'wss://private.invalid/?token=private-secret'
        errors = [ssl.SSLCertVerificationError(1, secret), OSError(secret),
                  RuntimeError(secret), TimeoutError(secret)]
        http_error = RuntimeError(secret)
        http_error.response = SimpleNamespace(status_code=401)
        errors.append(http_error)
        for error in errors:
            summary = bridge.connection_error_summary(error)
            self.assertNotIn('private', summary)
            self.assertNotIn('token', summary)
        self.assertIn('401', bridge.connection_error_summary(http_error))
        self.assertIn('TLS', bridge.connection_error_summary(errors[0]))

    def test_tool_dispatch_log_is_safe(self):
        with self.assertLogs(bridge.LOGGER, level='INFO') as capture:
            bridge.log_tool_request(json.dumps({'method': 'tools/call', 'params': {
                'name': 'get_nasdaq100_overview', 'arguments': {'secret': 'private-secret'}}}))
            bridge.log_tool_request(json.dumps({'method': 'tools/call', 'params': {
                'name': 'private-secret', 'arguments': {}}}))
        output = str(capture.output)
        self.assertIn('get_nasdaq100_overview', output)
        self.assertIn('未注册工具名', output)
        self.assertNotIn('private-secret', output)

    def test_endpoint_requires_tls_and_no_userinfo(self):
        for endpoint in [None, '', 'http://example.test', 'ws://example.test',
                         'wss://user:secret@example.test']:
            with self.assertRaises(ValueError):
                bridge.validate_endpoint(endpoint)
        self.assertEqual(bridge.validate_endpoint('wss://example.test/mcp?token=secret'),
                         'wss://example.test/mcp?token=secret')

    def test_jsonrpc_validation_and_utf8(self):
        line = bridge.jsonrpc_line('{"jsonrpc":"2.0","method":"测试"}')
        self.assertEqual(json.loads(line)['method'], '测试')
        for value in [b'\xff', '[]', '{}', 'null', '{', 'x' * (bridge.MAX_MESSAGE_SIZE + 1)]:
            with self.assertRaises(bridge.BridgeSessionEnded):
                bridge.jsonrpc_line(value)

    def test_log_redaction(self):
        async def check():
            reader = asyncio.StreamReader()
            reader.feed_data(b'https://example.test/?token=abc private-secret token=raw\n')
            reader.feed_eof()
            with self.assertLogs(bridge.LOGGER, level='WARNING') as capture:
                await bridge.stderr_to_log(SimpleNamespace(stderr=reader))
            for secret in ['example.test', 'private-secret', 'token=raw']:
                self.assertNotIn(secret, str(capture.output))
        asyncio.run(check())

    def test_missing_endpoint_exits_without_connection(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(bridge, 'load_dotenv'):
            with self.assertLogs(bridge.LOGGER, level='ERROR'):
                self.assertEqual(bridge.main(), 2)


class BridgeIntegration(unittest.IsolatedAsyncioTestCase):
    async def test_jsonrpc_roundtrip_and_disconnect_reaps_process(self):
        inbound, outbound = asyncio.Queue(), asyncio.Queue()
        processes = []

        class Socket:
            def __aiter__(self):
                return self

            async def __anext__(self):
                value = await inbound.get()
                if value is None:
                    raise StopAsyncIteration
                return json.dumps(value)

            async def send(self, value):
                await outbound.put(json.loads(value))

        @asynccontextmanager
        async def connect(*args, **kwargs):
            yield Socket()

        original = bridge.start_server

        async def start():
            process = await original()
            processes.append(process)
            return process

        async def receive_id(expected):
            async def get():
                while True:
                    response = await outbound.get()
                    if response.get('id') == expected:
                        return response
            return await asyncio.wait_for(get(), 20)

        with patch.object(bridge, 'connect', connect), patch.object(bridge, 'start_server', start), \
                patch.dict(os.environ, {'STOCK_DATA_MODE': 'demo'}):
            task = asyncio.create_task(bridge.bridge_once('wss://local-test.invalid/'))
            try:
                await inbound.put({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
                    'protocolVersion': '2024-11-05', 'capabilities': {},
                    'clientInfo': {'name': 'local-test', 'version': '1'}}})
                self.assertIn('result', await receive_id(1))
                await inbound.put({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
                await inbound.put({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'})
                result = await receive_id(2)
                self.assertEqual(len(result['result']['tools']), 4)
                await inbound.put({'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call', 'params': {
                    'name': 'get_nasdaq100_overview', 'arguments': {'days': 30}}})
                result = await receive_id(3)
                payload = result['result'].get('structuredContent') or json.loads(
                    result['result']['content'][0]['text'])
                self.assertTrue(payload['is_demo'])
                self.assertFalse(payload['data']['index_quote_available'])
                await inbound.put(None)
                with self.assertRaises(bridge.BridgeSessionEnded):
                    await asyncio.wait_for(task, 10)
            finally:
                if not task.done():
                    task.cancel()
                    await asyncio.gather(task, return_exceptions=True)
        self.assertTrue(processes)
        self.assertIsNotNone(processes[0].returncode)


if __name__ == '__main__':
    unittest.main()
