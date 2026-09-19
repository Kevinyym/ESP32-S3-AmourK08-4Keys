import hashlib
import http.client
import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from nas_music import BoundedThreadingHTTPServer, Library, make_handler


class NasMusicTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        base = Path(self.temp.name)
        self.music = base / "music"
        self.cache = base / "cache"
        self.music.mkdir()
        self.cache.mkdir()
        jay = self.music / "周杰伦" / "七里香"
        jay.mkdir(parents=True)
        (jay / "晴天.flac").write_bytes(b"not real flac")
        (self.music / "ignore.cue").write_text("FILE escape.flac", encoding="utf-8")
        self.library = Library(self.music, self.cache, "ffmpeg", 5)
        self.library.scan()

    def tearDown(self):
        self.temp.cleanup()

    def test_stable_id_and_only_ready_search(self):
        expected = hashlib.sha256("周杰伦/七里香/晴天.flac".encode()).hexdigest()
        self.assertEqual(self.library.health(), {"status": "ok", "indexed": 1, "ready": 0, "error": 0})
        self.assertEqual(self.library.search("晴天", 5), [])
        self.library.cache_path(expected).write_bytes(b"OggS-test")
        self.library.metadata_path(expected).write_text(
            json.dumps(self.library._fingerprint(self.library._tracks[expected])), encoding="utf-8"
        )
        self.library.scan()
        self.assertEqual(self.library.search("晴天", 5), [{"id": expected, "title": "晴天"}])
        self.assertEqual(self.library.search("周杰伦 晴天", 5),
                         [{"id": expected, "title": "晴天"}])
        self.assertEqual(self.library.search("周杰伦", 5), [{"id": expected, "title": "晴天"}])
        (self.music / "周杰伦" / "七里香" / "晴天.flac").write_bytes(b"changed source")
        self.library.scan()
        self.assertEqual(self.library.search("晴天", 5), [])

    def test_symlinks_are_not_indexed(self):
        outside = Path(self.temp.name) / "outside.flac"
        outside.write_bytes(b"x")
        (self.music / "link.flac").symlink_to(outside)
        self.library.scan()
        self.assertEqual(self.library.health()["indexed"], 1)

    def test_http_contract(self):
        track_id = next(iter(self.library._tracks))
        payload = b"OggS-audio"
        self.library.cache_path(track_id).write_bytes(payload)
        self.library.metadata_path(track_id).write_text(
            json.dumps(self.library._fingerprint(self.library._tracks[track_id])), encoding="utf-8"
        )
        self.library.scan()
        server = BoundedThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.library), 2)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{server.server_port}"
        try:
            with urllib.request.urlopen(base + "/health") as response:
                self.assertEqual(response.headers["Content-Type"], "application/json; charset=utf-8")
                self.assertEqual(json.load(response)["ready"], 1)
            query = urllib.parse.urlencode({"q": "晴天", "limit": 5})
            with urllib.request.urlopen(base + "/search?" + query) as response:
                self.assertEqual(json.load(response)["tracks"][0]["id"], track_id)
            with urllib.request.urlopen(base + f"/tracks/{track_id}.ogg") as response:
                self.assertEqual(response.headers["Content-Type"], "audio/ogg")
                self.assertEqual(int(response.headers["Content-Length"]), len(payload))
                self.assertEqual(response.read(), payload)
            # K08 keeps this connection open until it finishes consuming an
            # audio stream. Exercise two requests through one HTTP/1.1 socket.
            connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
            connection.request("GET", "/health")
            response = connection.getresponse()
            self.assertEqual(response.status, 200)
            response.read()
            connection.request("GET", f"/tracks/{track_id}.ogg")
            response = connection.getresponse()
            self.assertEqual(response.status, 200)
            self.assertEqual(response.read(), payload)
            connection.close()
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(base + "/tracks/../../etc/passwd.ogg")
            self.assertEqual(ctx.exception.code, 404)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
