#!/usr/bin/env python3
"""Exercise the shared C++ input parser: python tool/test_input.py --checker input_check.exe."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest

CHECKER = None


class InputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.input = self.root / "replay.in"
        self.rom = self.root / "rom.bin"
        self.rom.write_bytes(b"hello")

    def run_case(self, text, success=False, frames=1, extra=()):
        self.input.write_text(text, encoding="ascii")
        command = [CHECKER, str(self.rom), "--dump", str(self.root / "out"), "--input", str(self.input),
                   "--frames", str(frames), *extra]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0 if success else 2, result.stdout + result.stderr)
        if not success:
            self.assertTrue(result.stderr.strip(), "failure must include a diagnostic")
        return result.stdout

    def normalized(self, records="00 01 02 04 08\n"):
        # Size 16, hash identity for an all-zero synthetic header.
        data = bytes(16)
        self.rom.write_bytes(data)
        value = 14695981039346656037
        for byte in data:
            value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        return ("NESINPUT 1\nrom_size=16\nrom_hash=%016X\nfourscore=1\n"
                "port0=1\nport1=1\nport2=3\nram_init=0\ndata\n%s" % (value, records))

    def test_legacy_one_two_bytes_and_neutral_eof(self):
        self.assertIn("pads1=00000001", self.run_case("01\n", True, frames=3))
        self.assertIn("pads1=000080FF", self.run_case("# comment\n\nFF 80\r\n", True))

    def test_malformed_bytes_fields_and_lines(self):
        for text in ("", "garbage\n", "-1\n", "+1\n", "100\n", "00 z\n", "00 00 00\n",
                     "00 # suffix\n", "00" + " " * 512 + "\n", "NESINPUT 2\n", "00\x00garbage\n"):
            with self.subTest(text=text[:24]):
                self.run_case(text)

    def test_duplicate_cli_input(self):
        self.run_case("00\n", extra=("--input", str(self.input)))

    def test_versioned_pads_metadata_identity_and_eof(self):
        text = self.normalized()
        self.assertIn("pads1=08040201 command1=00 fourscore=1 ports=1,1,3", self.run_case(text, True))
        self.run_case(text, frames=2)
        self.rom.write_bytes(b"wrong ROM")
        self.run_case(text)

    def test_missing_duplicate_unknown_metadata(self):
        text = self.normalized()
        for mutated in (text.replace("ram_init=0\n", ""), text.replace("data\n", "port0=1\ndata\n"),
                        text.replace("port1=1", "port1=4"), text.replace("ram_init=0", "ram_init=2"),
                        text.replace("data\n", "unknown=0\ndata\n"), text.replace("port0=1", "port0=0")):
            with self.subTest(mutated=mutated[:80]):
                self.run_case(mutated)

    def test_reset_and_power_commands_are_parsed(self):
        for command in ("01", "02", "03"):
            with self.subTest(command=command):
                self.assertIn("command1=" + command, self.run_case(self.normalized(command + " 00 00 00 00\n"), True))

    def test_unsupported_commands_are_not_silently_discarded(self):
        for command in ("04", "08", "10", "FF"):
            with self.subTest(command=command):
                self.run_case(self.normalized(command + " 00 00 00 00\n"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checker", required=True)
    args = parser.parse_args()
    CHECKER = str(Path(args.checker).resolve())
    unittest.main(argv=[__file__])
