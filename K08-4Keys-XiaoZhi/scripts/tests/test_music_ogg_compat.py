"""Check the music wire format against the actual firmware Ogg parser."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('ffmpeg') and shutil.which('c++'), 'requires ffmpeg and c++')
class MusicOggCompatibility(unittest.TestCase):
    def test_mono_opus_packets_across_http_chunk_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            (temp / 'esp_log.h').write_text('\n'.join(
                '#define ESP_LOG' + level + '(...) ((void)0)' for level in 'DWEI'))
            (temp / 'check.cc').write_text(r'''
#include "ogg_demuxer.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <algorithm>
int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    assert(!bytes.empty());
    for (size_t chunk : {1, 7, 256, 4096}) {
        OggDemuxer parser;
        int packets = 0;
        parser.OnPacket([&](const uint8_t*, int rate, int duration, size_t size) {
            assert(rate == 24000 && duration == 60 && size > 0 && size <= 2048);
            ++packets;
        });
        for (size_t pos = 0; pos < bytes.size(); pos += chunk) {
            parser.Process(bytes.data() + pos, std::min(chunk, bytes.size() - pos));
        }
        assert(parser.Finish() && !parser.HasError() && packets > 0);
    }
}
''')
            demux = ROOT / 'main/audio/demuxer'
            subprocess.run(['c++', '-std=c++17', '-I', str(temp), '-I', str(demux),
                            str(temp / 'check.cc'), str(demux / 'ogg_demuxer.cc'),
                            '-o', str(temp / 'check')], check=True, capture_output=True, timeout=60)
            subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi',
                            '-i', 'sine=frequency=440:duration=2', '-ac', '1', '-ar', '24000',
                            '-c:a', 'libopus', '-b:a', '48k', '-frame_duration', '60',
                            str(temp / 'sample.ogg')], check=True, capture_output=True, timeout=30)
            subprocess.run([str(temp / 'check'), str(temp / 'sample.ogg')],
                           check=True, capture_output=True, timeout=30)


if __name__ == '__main__':
    unittest.main()
