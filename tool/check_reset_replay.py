#!/usr/bin/env python3
"""Check soft-reset and power-cycle replay commands on the reset_state probe.

    python tool/check_reset_replay.py --nes1 NES1 --fceux FCEUX --work OUTPUT

A NESINPUT 1 file applies soft reset, power cycle, soft reset and both bits together at fixed frames. nes1
and FCEUX must produce identical dumps for every frame and identical instruction traces around each
command, and the probe's recorded observations must match what the commands mean (spec O-41).
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_dumps import parse_dump
from compare_traces import compare_file

FRAMES = 90
COMMANDS = {12: 1, 30: 2, 50: 1, 70: 3}   # frame -> command bits (the first command frame is well after the boot)
BOOT = [0x00, 0x00, 0x00, 0xFD, 0xB4]      # A, X, Y, S, P (PHP image; TSX sets N) at the first boot after power
RETAINED = [0x11, 0x22, 0x33, 0xC0, 0xB4]  # after a soft reset: registers kept; the armed D and C flags are gone


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def trace_frames():
    frames = set()
    for frame in COMMANDS:
        frames.update((frame - 1, frame, frame + 1, frame + 2))
    return sorted(frames)


def expect(dump, frame, values, first=0x700):
    actual = [dump["RAM"][first + i] for i in range(len(values))]
    if actual != values:
        raise ValueError("frame %d RAM[%04X..]: got %s expected %s" % (
            frame, first, " ".join("%02X" % v for v in actual), " ".join("%02X" % v for v in values)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--nes1", required=True)
    parser.add_argument("--fceux", required=True)
    parser.add_argument("--work", required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    rom = repo / "test/probe/reset_state.nes"
    data = rom.read_bytes()
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    replay = work / "input.in"
    prefix = ("NESINPUT 1\nrom_size=%d\nrom_hash=%016X\nfourscore=0\nport0=1\nport1=1\nport2=0\nram_init=0\ndata\n" %
              (len(data), fnv1a64(data)))
    replay.write_text(prefix + "".join("%02X 00 00 00 00\n" % COMMANDS.get(f, 0) for f in range(1, FRAMES + 1)),
                      encoding="ascii")
    spec = ",".join(str(f) for f in trace_frames())
    for label, exe in (("nes1", args.nes1), ("fceux", args.fceux)):
        command = [exe, str(rom), "--dump", str(work / label), "--input", str(replay), "--frames", str(FRAMES),
                   "--dump-frames", "all", "--trace-frames", spec]
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=300)
    subprocess.run([sys.executable, str(repo / "tool/compare_dumps.py"), str(work / "nes1"), str(work / "fceux")],
                   check=True, timeout=300)
    instructions = 0
    for frame in trace_frames():
        file = "trace_f%06d.txt" % frame
        count, mismatch = compare_file(work / "nes1" / file, work / "fceux" / file)
        if mismatch:
            raise ValueError("frame %d: trace differs at instruction %d" % (frame, count + 1))
        instructions += count

    def dump(frame):
        return parse_dump(work / "nes1" / ("frame_f%06d.txt" % frame))

    expect(dump(11), 11, BOOT)
    expect(dump(20), 20, RETAINED)        # soft reset at 12: A/X/Y/S kept, P reduced to I
    if dump(20)["RAM"][0x705] != 0 or dump(20)["RAM"][0x709] != 0:
        raise ValueError("after a reset the APU status and an unenabled length load must read 0")
    expect(dump(40), 40, BOOT)            # power cycle at 30: everything back to power-on
    expect(dump(60), 60, RETAINED)        # soft reset at 50
    expect(dump(80), 80, BOOT)            # power and reset together at 70 end as a power-on boot
    print("reset/power replay identical: %d frames, %d trace instructions" % (FRAMES, instructions))
    (work / "results.json").write_text(json.dumps({"frames": FRAMES, "commands": COMMANDS,
                                                   "instructions": instructions}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as err:
        sys.exit("ERROR: %s" % err)
