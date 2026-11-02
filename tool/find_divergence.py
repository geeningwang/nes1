#!/usr/bin/env python3
"""Find where nes1 and FCEUX first diverge, automatically drilling down.

    python find_divergence.py --nes1 nes1.exe --fceux fceux64.exe --rom ROM [--frames N]
                              [--work DIR] [--init-pc ADDR] [--level frame|scanline]

Stages (see doc/parity_plan.md, section 3):
  1. digest run, one event per frame           -> first differing frame F
  2. re-run to frame F, digest of every scanline -> first differing scanline S
  3. re-run with the text instruction trace of frame F -> first differing instruction
  4. re-run with full dumps at scanline S (and S-1)    -> differing fields / addresses

With --level scanline, stage 1 uses one digest event per scanline for all frames instead (slower,
but also finds differences inside frames that disappear again before the frame ends).

Exit status: 0 = no divergence found, 1 = divergence found, 2 = a run failed.
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_dumps as cd  # noqa: E402


TIMEOUT_S = 300


def run_emu(exe, rom, outdir, frames, extra, init_pc, input_file=""):
    if os.path.isdir(outdir):
        shutil.rmtree(outdir)
    cmd = [exe, rom, "--dump", outdir, "--frames", str(frames)] + extra
    if init_pc:
        cmd += ["--init-pc", init_pc]
    if input_file:
        cmd += ["--input", input_file]
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True,
                           timeout=TIMEOUT_S)
    except subprocess.TimeoutExpired:
        print("ERROR: %s did not finish within %d s (hung?): %s" % (os.path.basename(exe), TIMEOUT_S, " ".join(cmd)))
        sys.exit(2)
    if r.returncode != 0:
        print("ERROR: %s exited with %d\n%s" % (os.path.basename(exe), r.returncode, r.stderr.strip()))
        sys.exit(2)


def run_both(args, tag, frames, extra):
    dirs = {}
    for name, exe in (("nes1", args.nes1), ("fceux", args.fceux)):
        d = os.path.join(args.work, tag, name)
        run_emu(exe, args.rom, d, frames, extra, args.init_pc, args.input)
        dirs[name] = d
    return dirs["nes1"], dirs["fceux"]


def first_digest_difference(da, db):
    A, B = cd.read_digest(da), cd.read_digest(db)
    if not A or not B:
        return ("empty digest", 0, None, None, [])
    for i in range(min(len(A), len(B))):
        if A[i][0] != B[i][0]:
            return ("sequence", i, A[i], B[i], [])
        if A[i][1].get("ALL") != B[i][1].get("ALL"):
            fields = [k for k in cd.DIGEST_FIELDS if A[i][1].get(k) != B[i][1].get(k)]
            return ("state", i, A[i], B[i], fields)
    if len(A) != len(B):
        return ("count", min(len(A), len(B)), None, None, [])
    return None


def describe(d):
    kind, i, a, b, fields = d
    if kind == "state":
        return "#%d %s  frame=%s scanline=%s  differing: %s" % (
            i + 1, a[0], a[1].get("f"), a[1].get("sl"), ", ".join(fields))
    return "%s mismatch at event #%d" % (kind, i + 1)


def compare_traces(ta, tb):
    with open(ta) as fa, open(tb) as fb:
        la, lb = fa.read().splitlines(), fb.read().splitlines()
    n = min(len(la), len(lb))
    for i in range(n):
        if la[i] != lb[i]:
            return i, la, lb
    if len(la) != len(lb):
        return n, la, lb
    return None


def trace_fields(line):
    names = ("PC", "OP", "A", "X", "Y", "S", "P", "CYCLES", "SCANLINE", "DB", "PL")
    return dict(zip(names, line.split()))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--nes1", required=True)
    ap.add_argument("--fceux", required=True)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--work", default="divergence_work")
    ap.add_argument("--init-pc", default="")
    ap.add_argument("--input", default="", help="identical replay file passed through every rerun")
    ap.add_argument("--level", choices=("frame", "scanline"), default="frame")
    ap.add_argument("--timeout", type=int, default=300, help="seconds allowed for one emulator run")
    args = ap.parse_args()
    global TIMEOUT_S
    TIMEOUT_S = args.timeout

    # Stage 1
    stage1 = ["--digest", "--dump-frames", "all"]
    if args.level == "scanline":
        stage1 += ["--dump-scanline-frames", "all"]
    da, db = run_both(args, "stage1", args.frames, stage1)
    d = first_digest_difference(da, db)
    if d is None:
        print("IDENTICAL: no difference in %d frames (%s level)" % (args.frames, args.level))
        return 0
    print("Stage 1 (%s digests): first difference: %s" % (args.level, describe(d)))
    if d[0] != "state":
        return 1
    frame = int(d[2][1]["f"])

    # Stage 2
    da, db = run_both(args, "stage2", frame, ["--digest", "--dump-scanline-frames", str(frame)])
    d2 = first_digest_difference(da, db)
    sl = None
    if d2 and d2[0] == "state":
        print("Stage 2 (scanlines of frame %d): first difference: %s" % (frame, describe(d2)))
        sl = int(d2[2][1]["sl"])
        if sl < 0:
            sl = None
    else:
        print("Stage 2: no scanline of frame %d differs; the difference arises outside the visible scanlines "
              "(vblank, pre-render, or end of frame)." % frame)

    # Stage 3
    ta, tb = run_both(args, "stage3", frame, ["--digest", "--trace-frames", str(frame)])
    tn = os.path.join("trace_f%06d.txt" % frame)
    td = compare_traces(os.path.join(ta, tn), os.path.join(tb, tn))
    if td is None:
        print("Stage 3: instruction traces of frame %d are identical (the difference is not CPU-visible)." % frame)
    else:
        i, la, lb = td
        print("Stage 3: first differing instruction is #%d of frame %d (trace line %d):" % (i, frame, i + 1))
        for k in range(max(0, i - 3), i):
            print("    both  %s" % la[k])
        x = la[i] if i < len(la) else "<missing>"
        y = lb[i] if i < len(lb) else "<missing>"
        print("    nes1  %s\n    fceux %s" % (x, y))
        if i < len(la) and i < len(lb):
            fa, fb = trace_fields(la[i]), trace_fields(lb[i])
            print("    differs in: %s" % ", ".join(k for k in fa if fa.get(k) != fb.get(k)))

    # Stage 4
    if sl is not None:
        pick = sorted({max(sl - 1, 0), sl})
        fa, fb = run_both(args, "stage4", frame, ["--dump-scanline-frames", str(frame), "--dump-scanlines",
                                                ",".join(str(p) for p in pick)])
        for p in pick:
            name = "scanline_f%06d_s%03d.txt" % (frame, p)
            lines, _ = cd.compare_event(os.path.join(fa, name), os.path.join(fb, name), set())
            print("Stage 4: %s: %s" % (name, "identical" if not lines else ""))
            for ln in lines:
                print(ln)
    return 1


if __name__ == "__main__":
    sys.exit(main())
