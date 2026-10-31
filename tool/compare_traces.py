#!/usr/bin/env python3
"""Compare the instruction-by-instruction text traces of nes1 and FCEUX for chosen frames.

    python compare_traces.py --nes1 nes1.exe --fceux fceux64.exe --rom ROM [--frames N]
                             [--trace-frames LIST] [--work DIR] [--init-pc ADDR]

Both emulators run N frames (default 3000) and write trace_fNNNNNN.txt for the frames in LIST
(default: 1-8,60,120,300,600,1000,1500,2000,2500,3000), one line per instruction:
    PC OP A X Y S P CYCLES SCANLINE DB PL
Every line of every chosen frame is compared (not only the chained hash).  For a frame that differs the
first differing instruction is shown with its neighbours and the differing columns.

Exit status: 0 = all traces identical, 1 = a difference was found, 2 = a run failed.
"""
import argparse
import os
import shutil
import subprocess
import sys

COLS = ("PC", "OP", "A", "X", "Y", "S", "P", "CYCLES", "SCANLINE", "DB", "PL")
DEFAULT_FRAMES = "1-8,60,120,300,600,1000,1500,2000,2500,3000"


def frame_list(spec):
    out = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-")
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def run_emu(exe, rom, outdir, frames, trace_spec, init_pc, timeout):
    if os.path.isdir(outdir):
        shutil.rmtree(outdir)
    cmd = [exe, rom, "--dump", outdir, "--frames", str(frames), "--digest", "--trace-frames", trace_spec]
    if init_pc:
        cmd += ["--init-pc", init_pc]
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        print("ERROR: %s did not finish within %d s" % (os.path.basename(exe), timeout))
        sys.exit(2)
    if r.returncode != 0:
        print("ERROR: %s exited with %d\n%s" % (os.path.basename(exe), r.returncode, r.stderr.strip()))
        sys.exit(2)


def compare_file(pa, pb):
    """Returns (instructions compared, None) or (index, (lines of A, lines of B)) at the first difference."""
    with open(pa) as fa, open(pb) as fb:
        la, lb = fa.read().splitlines(), fb.read().splitlines()
    la = [l for l in la if not l.startswith("#")]
    lb = [l for l in lb if not l.startswith("#")]
    for i in range(min(len(la), len(lb))):
        if la[i] != lb[i]:
            return i, (la, lb)
    if len(la) != len(lb):
        return min(len(la), len(lb)), (la, lb)
    return len(la), None


def show(i, la, lb):
    lo = max(0, i - 2)
    for k in range(lo, i):
        print("      both   %s" % la[k])
    ra = la[i] if i < len(la) else "(trace ends)"
    rb = lb[i] if i < len(lb) else "(trace ends)"
    print("      nes1   %s" % ra)
    print("      fceux  %s" % rb)
    if i < len(la) and i < len(lb):
        fa, fb = ra.split(), rb.split()
        diff = [COLS[k] for k in range(min(len(fa), len(fb), len(COLS))) if fa[k] != fb[k]]
        print("      differs in: %s" % ", ".join(diff))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--nes1", required=True)
    ap.add_argument("--fceux", required=True)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--trace-frames", default=DEFAULT_FRAMES)
    ap.add_argument("--work", default="trace_work")
    ap.add_argument("--init-pc", default="")
    ap.add_argument("--timeout", type=int, default=600)
    args = ap.parse_args()

    frames = [f for f in frame_list(args.trace_frames) if f <= args.frames]
    spec = ",".join(str(f) for f in frames)
    dirs = {}
    for name, exe in (("nes1", args.nes1), ("fceux", args.fceux)):
        d = os.path.join(args.work, name)
        run_emu(exe, args.rom, d, args.frames, spec, args.init_pc, args.timeout)
        dirs[name] = d

    total, bad = 0, 0
    for f in frames:
        fn = "trace_f%06d.txt" % f
        pa, pb = os.path.join(dirs["nes1"], fn), os.path.join(dirs["fceux"], fn)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            print("  frame %d: trace missing (nes1: %s, fceux: %s)" % (f, os.path.exists(pa), os.path.exists(pb)))
            bad += 1
            continue
        n, res = compare_file(pa, pb)
        if res is None:
            total += n
        else:
            bad += 1
            print("  frame %d: DIFFERS at instruction %d" % (f, n + 1))
            show(n, *res)
    if bad:
        print("TRACE MISMATCH: %d of %d frames differ" % (bad, len(frames)))
        return 1
    print("TRACES IDENTICAL: %d frames, %d instructions compared line by line" % (len(frames), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
