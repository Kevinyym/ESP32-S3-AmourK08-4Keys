#!/usr/bin/env python3
"""Dependency-free HTTP service for a read-only NAS music library."""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from socketserver import ThreadingMixIn
from urllib.parse import parse_qs, unquote, urlsplit

LOG = logging.getLogger("nas-music")
TRACK_RE = re.compile(r"^/tracks/([0-9a-f]{64})\.ogg$")
MAX_TITLE_CHARS = 160
ENCODING_VERSION = "opus-mono-24000-48k-60ms-v1"


@dataclass(frozen=True)
class Track:
    id: str
    title: str
    search_text: str
    source: Path


class Library:
    def __init__(self, music_dir: Path, cache_dir: Path, ffmpeg: str, timeout: int):
        self.music_dir = music_dir.resolve(strict=True)
        self.cache_dir = cache_dir.resolve(strict=True)
        self.ffmpeg = ffmpeg
        self.timeout = timeout
        self._lock = threading.Lock()
        self._tracks: dict[str, Track] = {}
        self._ready: set[str] = set()
        self._errors: dict[str, str] = {}
        self._worker: threading.Thread | None = None

    def scan(self) -> None:
        tracks: dict[str, Track] = {}
        for root, dirs, files in os.walk(self.music_dir, followlinks=False):
            root_path = Path(root)
            dirs[:] = sorted(d for d in dirs if not (root_path / d).is_symlink())
            for name in sorted(files):
                source = root_path / name
                if source.suffix.casefold() != ".flac" or source.is_symlink():
                    continue
                try:
                    resolved = source.resolve(strict=True)
                    relative = resolved.relative_to(self.music_dir).as_posix()
                except (OSError, ValueError):
                    LOG.warning("skipping path outside library: %s", source)
                    continue
                track_id = hashlib.sha256(relative.encode("utf-8")).hexdigest()
                title = source.stem[:MAX_TITLE_CHARS]
                # Keep the display title concise, but index the complete
                # library-relative path. A common collection layout is
                # "artist/album/song.flac", where the song filename alone
                # cannot answer an artist query.
                search_text = relative.removesuffix(source.suffix)
                tracks[track_id] = Track(track_id, title, search_text, resolved)
        ready = {track.id for track in tracks.values() if self._cache_matches(track)}
        with self._lock:
            self._tracks = tracks
            self._ready = ready
            self._errors = {}
        LOG.info("indexed %d tracks; %d already ready", len(tracks), len(ready))

    def cache_path(self, track_id: str) -> Path:
        return self.cache_dir / f"{track_id}.ogg"

    def metadata_path(self, track_id: str) -> Path:
        return self.cache_dir / f"{track_id}.json"

    def _fingerprint(self, track: Track) -> dict[str, int | str]:
        stat = track.source.stat()
        return {"version": ENCODING_VERSION, "size": stat.st_size, "mtime_ns": stat.st_mtime_ns}

    def _cache_matches(self, track: Track) -> bool:
        try:
            cached = self.cache_path(track.id)
            metadata = self.metadata_path(track.id)
            if cached.is_symlink() or metadata.is_symlink() or cached.stat().st_size == 0:
                return False
            with metadata.open("r", encoding="utf-8") as source:
                return json.load(source) == self._fingerprint(track)
        except (OSError, ValueError, json.JSONDecodeError):
            return False

    def start_conversion(self) -> None:
        self._worker = threading.Thread(target=self._convert_all, name="converter", daemon=True)
        self._worker.start()

    def _convert_all(self) -> None:
        with self._lock:
            pending = [track for tid, track in self._tracks.items() if tid not in self._ready]
        for track in pending:
            self._convert(track)

    def _convert(self, track: Track) -> None:
        final = self.cache_path(track.id)
        temporary = self.cache_dir / f".{track.id}.{os.getpid()}.tmp.ogg"
        metadata = self.metadata_path(track.id)
        metadata_tmp = self.cache_dir / f".{track.id}.{os.getpid()}.tmp.json"
        try:
            resolved = track.source.resolve(strict=True)
            resolved.relative_to(self.music_dir)
            before = self._fingerprint(track)
            command = [
                self.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                "-protocol_whitelist", "file,pipe",
                "-i", os.fspath(resolved), "-map", "0:a:0", "-vn", "-ac", "1",
                "-ar", "24000", "-c:a", "libopus", "-b:a", "48k",
                "-application", "audio", "-frame_duration", "60", "-vbr", "on",
                "-f", "ogg", os.fspath(temporary),
            ]
            subprocess.run(command, check=True, timeout=self.timeout, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
            if not temporary.is_file() or temporary.stat().st_size == 0:
                raise RuntimeError("ffmpeg produced an empty file")
            if self._fingerprint(track) != before:
                raise RuntimeError("source changed during conversion")
            with metadata_tmp.open("w", encoding="utf-8") as output:
                json.dump(before, output, separators=(",", ":"))
            os.replace(temporary, final)
            os.replace(metadata_tmp, metadata)
            with self._lock:
                self._ready.add(track.id)
                self._errors.pop(track.id, None)
            LOG.info("ready: %s", track.title)
        except (OSError, ValueError, subprocess.SubprocessError, RuntimeError) as exc:
            temporary.unlink(missing_ok=True)
            metadata_tmp.unlink(missing_ok=True)
            if isinstance(exc, subprocess.TimeoutExpired):
                detail = "timeout"
            elif isinstance(exc, subprocess.CalledProcessError):
                detail = "ffmpeg_failed"
            elif isinstance(exc, (OSError, ValueError)):
                detail = "source_or_cache_unavailable"
            else:
                detail = "invalid_output"
            with self._lock:
                self._errors[track.id] = detail
            LOG.error("conversion failed for track %s: %s", track.id, detail)

    def _matching_tracks(self, query: str) -> list[Track]:
        needle = query.casefold().strip()
        # The voice backend commonly sends "artist song" while library titles
        # use separators such as "artist - song", or stores the artist only in
        # a parent directory. Match each meaningful token independently against
        # the full relative path so punctuation and directory layout do not
        # turn a valid request into an empty result.
        terms = [term for term in re.split(r"[^0-9a-z\u4e00-\u9fff]+", needle) if term]
        with self._lock:
            tracks = [t for tid, t in self._tracks.items() if tid in self._ready]
        if terms:
            tracks = [t for t in tracks if all(term in t.search_text.casefold() for term in terms)]
        tracks.sort(key=lambda t: (not t.search_text.casefold().startswith(needle),
                                   t.title.casefold(), t.id))
        return tracks

    def search(self, query: str, limit: int) -> list[dict[str, str]]:
        tracks = self._matching_tracks(query)
        return [{"id": t.id, "title": t.title} for t in tracks[:limit]]

    def search_with_total(self, query: str, limit: int) -> tuple[list[dict[str, str]], int]:
        tracks = self._matching_tracks(query)
        return ([{"id": t.id, "title": t.title} for t in tracks[:limit]], len(tracks))

    def ready_path(self, track_id: str) -> Path | None:
        with self._lock:
            if track_id not in self._ready:
                return None
        path = self.cache_path(track_id)
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(self.cache_dir)
            return resolved if resolved.is_file() else None
        except (OSError, ValueError):
            return None

    def health(self) -> dict[str, int | str]:
        with self._lock:
            return {"status": "ok", "indexed": len(self._tracks), "ready": len(self._ready), "error": len(self._errors)}


class BoundedThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True

    def __init__(self, address, handler, max_clients: int):
        self._slots = threading.BoundedSemaphore(max_clients)
        super().__init__(address, handler)

    def process_request(self, request, client_address):
        if not self._slots.acquire(blocking=False):
            try:
                request.sendall(
                    b"HTTP/1.1 503 Service Unavailable\r\n"
                    b"Content-Type: application/json; charset=utf-8\r\n"
                    b"Content-Length: 16\r\nConnection: close\r\n\r\n"
                    b'{"error":"busy"}'
                )
            except OSError:
                pass
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self._slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._slots.release()

    def handle_error(self, request, client_address):
        # A keep-alive client can disconnect while the handler is waiting for
        # the next request. This is normal and should not produce a traceback
        # in the NAS container log.
        error_type, error, _ = sys.exc_info()
        if error_type and issubclass(error_type, (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


def make_handler(library: Library):
    class Handler(BaseHTTPRequestHandler):
        server_version = "NasMusic/1.0"
        protocol_version = "HTTP/1.1"

        def setup(self):
            super().setup()
            self.connection.settimeout(10)

        def do_GET(self):
            parsed = urlsplit(self.path)
            if parsed.path == "/health":
                self._json(HTTPStatus.OK, library.health())
                return
            if parsed.path == "/search":
                params = parse_qs(parsed.query, keep_blank_values=True)
                query = params.get("q", [""])[0]
                try:
                    limit = int(params.get("limit", ["5"])[0])
                except ValueError:
                    self._json(HTTPStatus.BAD_REQUEST, {"error": "limit must be an integer"})
                    return
                if not 1 <= limit <= 5:
                    self._json(HTTPStatus.BAD_REQUEST, {"error": "limit must be between 1 and 5"})
                    return
                tracks, total = library.search_with_total(query, limit)
                self._json(HTTPStatus.OK, {"tracks": tracks, "total": total})
                return
            match = TRACK_RE.fullmatch(unquote(parsed.path))
            if match:
                path = library.ready_path(match.group(1))
                if path is None:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "track not ready or not found"})
                    return
                self._file(path)
                return
            self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})

        def _json(self, status: HTTPStatus, value) -> None:
            body = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def _file(self, path: Path) -> None:
            try:
                source = path.open("rb")
                size = os.fstat(source.fileno()).st_size
            except OSError:
                self._json(HTTPStatus.NOT_FOUND, {"error": "track not found"})
                return
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "audio/ogg")
            self.send_header("Content-Length", str(size))
            self.send_header("Cache-Control", "public, max-age=31536000, immutable")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                with source:
                    shutil.copyfileobj(source, self.wfile, length=64 * 1024)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, fmt, *args):
            LOG.info("%s - %s", self.address_string(), fmt % args)

    return Handler


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--music-dir", type=Path, default=Path(os.getenv("MUSIC_DIR", "/music")))
    parser.add_argument("--cache-dir", type=Path, default=Path(os.getenv("CACHE_DIR", "/cache")))
    parser.add_argument("--host", default=os.getenv("HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.getenv("PORT", "8090")))
    parser.add_argument("--ffmpeg", default=os.getenv("FFMPEG", "ffmpeg"))
    parser.add_argument("--timeout", type=int, default=int(os.getenv("TRANSCODE_TIMEOUT", "1800")))
    parser.add_argument("--max-clients", type=int, default=int(os.getenv("MAX_CLIENTS", "4")))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    args.cache_dir.mkdir(parents=True, exist_ok=True)
    library = Library(args.music_dir, args.cache_dir, args.ffmpeg, args.timeout)
    library.scan()
    library.start_conversion()
    server = BoundedThreadingHTTPServer((args.host, args.port), make_handler(library), args.max_clients)
    signal.signal(signal.SIGTERM, lambda *_: threading.Thread(target=server.shutdown).start())
    LOG.info("listening on %s:%d", args.host, args.port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
