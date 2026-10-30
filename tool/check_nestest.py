#!/usr/bin/env python3
"""Check an emulator's instruction trace against test/nestest.log (the independent reference).

    python check_nestest.py <trace_f000001.txt> [--log test/nestest.log.txt] [--emulator-start-cycle 0]

The trace comes from:  <exe> test/nestest.nes --dump OUT --init-pc C000 --frames 1 --trace-frames 1 --digest
Compares PC, A, X, Y, P, S and the cycle count of every instruction in the log.  nestest.log counts from
cycle 7 (the reset sequence); the emulators start at cycle 0, so the log cycle is shifted by 7.
Exit status 0 if every log line matches, 1 otherwise.
"""
import argparse
import os
import re
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--log", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test", "nestest.log.txt"))
    ap.add_argument("--cycle-shift", type=int, default=7)
    ap.add_argument("--show", type=int, default=5, help="max mismatches to print")
    args = ap.parse_args()

    ref = []
    pat = re.compile(r"^([0-9A-F]{4}).*A:([0-9A-F]{2}) X:([0-9A-F]{2}) Y:([0-9A-F]{2}) P:([0-9A-F]{2}) SP:([0-9A-F]{2}) .*CYC:(\d+)")
    with open(args.log) as f:
        for ln in f:
            m = pat.match(ln)
            if m:
                pc, a, x, y, p, sp, cyc = m.groups()
                ref.append((pc, a, x, y, p, sp, int(cyc) - args.cycle_shift, ln.rstrip()))

    got = []
    with open(args.trace) as f:
        for ln in f:
            if ln.startswith("#") or not ln.strip():
                continue
            t = ln.split()
            got.append((t[0], t[2], t[3], t[4], t[6], t[5], int(t[7])))

    bad = 0
    for i, r in enumerate(ref):
        if i >= len(got):
            print("trace ended after %d instructions; log has %d" % (len(got), len(ref)))
            bad += 1
            break
        g = got[i]
        if g != r[:7]:
            bad += 1
            if bad <= args.show:
                print("mismatch at instruction %d (log line %d):" % (i + 1, i + 1))
                print("  log : %s" % r[7])
                print("  got : PC=%s A=%s X=%s Y=%s P=%s SP=%s CYC=%d (log-shifted CYC=%d)" % (g + (r[6],))[:8])
    n = len(ref)
    print("%s: %d of %d log instructions match" % ("OK" if bad == 0 else "FAIL", n - bad if bad < n else 0, n))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
