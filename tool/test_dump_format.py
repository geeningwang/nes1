#!/usr/bin/env python3
"""Check that the comparison tools see every NESDUMP 3 section and reject malformed dumps.

    python tool/test_dump_format.py --checker dump_check.exe
"""
import argparse
import contextlib
import io
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_dumps
import find_divergence

CHECKER = None
# mutation -> section reported by the full dump comparison / field reported by the digest comparison
MUTATIONS = {"cpu": ("CPU", "CPU"), "ppu": ("PPU", "PPU"), "ram": ("RAM", "RAM"), "wram": ("WRAM", "WRAM"),
             "nt": ("NT", "NT"), "ntv": ("NTV", "NTV"), "io": ("IO", "IO"), "irq": ("IRQ", "IRQ"),
             "oam": ("OAM", "OAM"), "pal": ("PAL", "PAL"), "chr": ("CHR", "CHR"), "fb": ("FB", "FB")}


def compare(a, b):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = compare_dumps.main([str(a), str(b)])
    return code, out.getvalue() + err.getvalue()


class DumpFormatTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rom = self.root / "rom.bin"
        self.rom.write_bytes(b"x")

    def make(self, name, mutate="", digest=False):
        import subprocess
        out = self.root / name
        command = [CHECKER, str(self.rom), "--dump", str(out), "--frames", "1", "--dump-frames", "1"]
        if digest:
            command.append("--digest")
        if mutate:
            command += ["--mutate", mutate]
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)
        return out

    def test_identical_dumps_compare_equal(self):
        for digest in (False, True):
            with self.subTest(digest=digest):
                code, text = compare(self.make("a", digest=digest), self.make("b", digest=digest))
                self.assertEqual(code, 0, text)

    def test_every_section_is_detected(self):
        for digest in (False, True):
            base = self.make("base_%d" % digest, digest=digest)
            for name, (section, field) in MUTATIONS.items():
                with self.subTest(mutation=name, digest=digest):
                    other = self.make("%s_%d" % (name, digest), mutate=name, digest=digest)
                    code, text = compare(base, other)
                    self.assertEqual(code, 1, text)
                    if digest:
                        self.assertIsNotNone(find_divergence.first_digest_difference(str(base), str(other)))
                        self.assertIn(field, find_divergence.first_digest_difference(str(base), str(other))[4])
                    else:
                        self.assertRegex(text, r"\n    %-6s" % section)

    def edit(self, folder, fn, text):
        path = Path(folder) / fn
        path.write_text(text, newline="")

    def test_old_version_is_rejected(self):
        a, b = self.make("a"), self.make("b")
        path = a / "frame_f000001.txt"
        path.write_text(path.read_text().replace("NESDUMP 3", "NESDUMP 2", 1), newline="")
        code, text = compare(a, b)
        self.assertEqual(code, 2, text)
        self.assertIn("NESDUMP 3", text)

    def test_missing_and_truncated_sections_are_rejected(self):
        for index, (label, pattern) in enumerate((("missing WRAM", r"\[WRAM\].*?(?=\[NT\])"),
                                                  ("missing IRQ", r"\[IRQ\].*?(?=\[PAL\])"),
                                                  ("no END", r"END\n$"))):
            with self.subTest(label=label):
                a, b = self.make("a_%d" % index), self.make("b_%d" % index)
                path = a / "frame_f000001.txt"
                text = path.read_text()
                changed = re.sub(pattern, "", text, count=1, flags=re.S)
                self.assertNotEqual(text, changed, label)
                path.write_text(changed, newline="")
                code, out = compare(a, b)
                self.assertEqual(code, 2, out)

    def test_short_section_is_rejected(self):
        a, b = self.make("a"), self.make("b")
        path = a / "frame_f000001.txt"
        text = path.read_text()
        changed = text.replace("[IO] 0000-000F\n0000: 01 02", "[IO] 0000-000F\n", 1)
        self.assertNotEqual(text, changed)
        path.write_text(changed, newline="")
        self.assertEqual(compare(a, b)[0], 2)

    def test_mixed_digest_and_full_dump_is_rejected(self):
        code, text = compare(self.make("a"), self.make("b", digest=True))
        self.assertEqual(code, 2, text)

    def test_manifest_must_match_events(self):
        for digest, events in ((False, "events.txt"), (True, "digest.txt")):
            with self.subTest(digest=digest):
                a, b = self.make("a%d" % digest, digest=digest), self.make("b%d" % digest, digest=digest)
                run = a / "run.txt"
                run.write_text(run.read_text().replace("events: frame=1", "events: frame=2"), newline="")
                code, text = compare(a, b)
                self.assertEqual(code, 2, text)

    def test_old_digest_version_and_malformed_lines_are_rejected(self):
        a, b = self.make("a", digest=True), self.make("b", digest=True)
        digest = (a / "digest.txt").read_text()
        (a / "digest.txt").write_text(digest.replace("NESDIGEST 3", "NESDIGEST 2"), newline="")
        self.assertEqual(compare(a, b)[0], 2)
        (a / "digest.txt").write_text(re.sub(r" IRQ=\w+", "", digest), newline="")
        self.assertEqual(compare(a, b)[0], 2)

    def test_empty_digest_is_rejected(self):
        a, b = self.make("a", digest=True), self.make("b", digest=True)
        (a / "digest.txt").write_text("NESDIGEST 3\n", newline="")
        self.assertEqual(find_divergence.first_digest_difference(str(a), str(b))[0], "empty digest")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checker", required=True)
    args = parser.parse_args()
    CHECKER = str(Path(args.checker).resolve())
    unittest.main(argv=[__file__])
