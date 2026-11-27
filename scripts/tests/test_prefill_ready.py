"""Host-test the audio pre-roll release condition (AudioService::PrefillReady).

The pre-roll gate in AudioOutputTask releases the first frame when the queue
holds a full pre-roll, or when the timeout elapses (a short reply never fills
the queue). This test compiles the real PrefillReady function from
main/audio/audio_service.h on the host and checks its boundary conditions, so
a regression in the release condition (e.g. dropping the timeout branch and
stalling short replies until the watchdog kills them) fails here, not on the
device. The thresholds are read from the header, so the test tracks the
constants instead of hard-coding them.
"""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "main/audio/audio_service.h"


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@unittest.skipUnless(shutil.which("c++"), "requires a host C++ compiler")
class PrefillReadyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        header = HEADER.read_text()
        frames = re.search(r"kPlaybackPrefillFrames\s*=\s*(\d+)", header).group(1)
        timeout = re.search(r"kPrefillTimeoutUs\s*=\s*(\d+)\s*\*\s*1000", header).group(1)
        cls.frames = int(frames)
        cls.timeout_ms = int(timeout)
        cls.timeout_us = cls.timeout_ms * 1000

        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.binary = str(pathlib.Path(cls.tmp.name) / "prefill-ready-test")
        prefill = function(header, "static bool PrefillReady(")
        source = f"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
struct AudioService {{
    static constexpr size_t kPlaybackPrefillFrames = {cls.frames};
    static constexpr int64_t kPrefillTimeoutUs = {cls.timeout_ms} * 1000;
{prefill}
}};
int main() {{
    using A = AudioService;
    // Queue full releases immediately, even before the timeout.
    assert(A::PrefillReady({cls.frames}, 0));
    assert(A::PrefillReady({cls.frames} + 1, 0));
    // Below a full pre-roll and before the timeout: keep waiting.
    assert(!A::PrefillReady(0, 0));
    assert(!A::PrefillReady({cls.frames} - 1, 0));
    assert(!A::PrefillReady({cls.frames} - 1, {cls.timeout_us} - 1));
    // Timeout releases even a short reply that never fills the queue.
    assert(A::PrefillReady(0, {cls.timeout_us}));
    assert(A::PrefillReady(1, {cls.timeout_us}));
    assert(A::PrefillReady({cls.frames} - 1, {cls.timeout_us}));
    printf("PrefillReady OK (frames=%d, timeout=%dms)\\n", {cls.frames}, {cls.timeout_ms});
    return 0;
}}
"""
        src = pathlib.Path(cls.tmp.name) / "prefill_ready_test.cc"
        src.write_text(source)
        subprocess.run(
            ["c++", "-std=c++17", "-Wall", "-Wextra", "-o", cls.binary, str(src)],
            check=True,
        )

    def test_prefill_ready_boundaries(self):
        result = subprocess.run([self.binary], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PrefillReady OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
