#!/usr/bin/env python3
"""Compare controller_serial probe reports and traces under different attachments.

    python tool/check_input_replay.py --nes1 NES1 --fceux FCEUX --work OUTPUT

The probe ROM is generated from our own source by make_probe_rom.py.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_dumps import parse_dump
from compare_traces import compare_file


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--nes1", required=True)
    parser.add_argument("--fceux", required=True)
    parser.add_argument("--work", required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    rom = repo / "test/probe/controller_serial.nes"
    data = rom.read_bytes()
    results = []
    for name, fs, expansion, ports, pads in (
        ("two_pads", 0, 0, (1, 1), (0xA5, 0x5A, 0, 0)),
        ("four_score", 1, 0, (1, 1), (0x81, 0x42, 0x24, 0x18)),
        ("famicom4", 0, 3, (1, 1), (0x10, 0x20, 0x40, 0x80)),
        ("both_adapters", 1, 3, (1, 1), (0xF0, 0x0F, 0x55, 0xAA)),
        ("port2_disconnected", 0, 0, (1, 0), (0xFF, 0, 0, 0)),
    ):
        work = Path(args.work) / name
        work.mkdir(parents=True, exist_ok=True)
        replay = work / "input.in"
        prefix = ("NESINPUT 1\nrom_size=%d\nrom_hash=%016X\nfourscore=%d\n"
                  "port0=%d\nport1=%d\nport2=%d\nram_init=0\ndata\n" %
                  (len(data), fnv1a64(data), fs, ports[0], ports[1], expansion))
        record = "00 " + " ".join("%02X" % byte for byte in pads) + "\n"
        replay.write_text(prefix + record * 4, encoding="ascii")
        for label, exe in (("nes1", args.nes1), ("fceux", args.fceux)):
            command = [exe, str(rom), "--dump", str(work / label), "--input", str(replay),
                       "--frames", "4", "--dump-frames", "4", "--trace-frames", "1-4"]
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
        subprocess.run([sys.executable, str(repo / "tool/compare_dumps.py"),
                        str(work / "nes1"), str(work / "fceux")], check=True, timeout=60)
        instructions = 0
        for frame in range(1, 5):
            file = "trace_f%06d.txt" % frame
            count, mismatch = compare_file(work / "nes1" / file, work / "fceux" / file)
            if mismatch:
                raise ValueError("%s frame %d: trace differs at instruction %d" % (name, frame, count + 1))
            instructions += count
        dump = parse_dump(work / "nes1/frame_f000004.txt")
        for port in range(2):
            for bit in range(24):
                value = 0
                if ports[port]:
                    value = ((pads[port] >> bit) & 1) if bit < 8 else (
                        ((pads[2 + port] >> (bit - 8)) & 1) if bit < 16 else 0)
                    if fs and bit == 19 - port:
                        value |= 1
                    if not fs and bit >= 8:
                        value |= 1
                if expansion == 3:
                    value = (value & 1) | (2 if bit >= 8 else ((pads[2 + port] >> bit) & 1) << 1)
                expected = 0x40 | value
                actual = dump["RAM"][0x400 + port * 0x100 + bit]
                if actual != expected:
                    raise ValueError("%s port %d bit %d: got %02X expected %02X" %
                                     (name, port + 1, bit, actual, expected))
        print("%s: reports and %d trace instructions match" % (name, instructions))
        results.append({"case": name, "frames": 4, "instructions": instructions})
    (Path(args.work) / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as err:
        sys.exit("ERROR: %s" % err)
