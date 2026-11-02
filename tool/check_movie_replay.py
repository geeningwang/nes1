#!/usr/bin/env python3
"""Verify normalized replay against FCEUX's native FM2 playback.

    python tool/check_movie_replay.py --fceux EXE --rom ROM --movie FM2 --input NESINPUT
                                     --frames 3000 --work OUTPUT
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_dumps import read_digest
from compare_traces import compare_file


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for arg in ("fceux", "rom", "movie", "input", "work"):
        parser.add_argument("--" + arg, required=True)
    parser.add_argument("--frames", type=int, default=3000)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    if args.frames < 1:
        parser.error("--frames must be positive")
    selected = sorted({f for f in (1, 2, 3, 4, 5, 6, 7, 8, 120, 1502, args.frames) if f <= args.frames})
    spec = ",".join(str(f) for f in selected)
    work = Path(args.work)
    for label, option, source in (("native", "--movie", args.movie), ("normalized", "--input", args.input)):
        command = [args.fceux, args.rom, option, source, "--dump", str(work / label),
                   "--frames", str(args.frames), "--digest", "--dump-frames", "all", "--trace-frames", spec]
        result = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout)
        if result.returncode:
            raise ValueError("%s exited %d: %s\n%s" %
                             (label, result.returncode, result.stdout, result.stderr))
    a = read_digest(work / "native")
    b = read_digest(work / "normalized")
    if len(a) != args.frames or len(b) != args.frames:
        raise ValueError("incomplete frame digests: %d / %d, expected %d" % (len(a), len(b), args.frames))
    for index, (left, right) in enumerate(zip(a, b), 1):
        if left != right:
            raise ValueError("native and normalized replay differ at frame %d" % index)
    total = 0
    for frame in selected:
        file = "trace_f%06d.txt" % frame
        count, mismatch = compare_file(work / "native" / file, work / "normalized" / file)
        if mismatch:
            raise ValueError("frame %d trace differs at instruction %d" % (frame, count + 1))
        total += count
    evidence = {
        "frames": args.frames, "trace_frames": selected, "instructions": total,
        "sha256": {name: hashlib.sha256(Path(path).read_bytes()).hexdigest()
                   for name, path in (("rom", args.rom), ("movie", args.movie),
                                      ("input", args.input), ("fceux", args.fceux))},
    }
    (work / "verified.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("NATIVE REPLAY IDENTICAL: %d frame digests; %d trace frames; %d instructions" %
          (args.frames, len(selected), total))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as err:
        sys.exit("ERROR: %s" % err)
