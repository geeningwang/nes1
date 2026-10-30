#!/usr/bin/env python3
"""Compare two NESDUMP output folders (e.g. nes1 vs FCEUX) and report where they differ.

    python3 tool/compare_dumps.py <dirA> <dirB> [--max N] [--all] [--sections CPU,PPU,RAM,...]

Both folders come from the same command line (see doc/state_dump.md), so they contain
the same event files.  Events are compared in the chronological order recorded in
events.txt.  For each differing event the report lists the differing CPU/PPU fields
and, for memory sections, the exact addresses and byte values that differ.

Exit status: 0 if the dumps are identical, 1 if they differ, 2 on usage/IO errors.
"""
import argparse
import os
import re
import sys

HEX_SECTIONS = ("APU", "RAM", "NT", "PAL", "OAM", "CHR", "FB", "PRG")


def parse_dump(path):
    """Return {'header': {k: v}, 'CPU': {k: v}, 'PPU': {k: v}, 'APU'..: {addr: byte}, 'CRC': {k: v}}."""
    out = {"header": {}, "CPU": {}, "PPU": {}, "BUS": {}, "CRC": {}}
    section = None
    with open(path, "r", newline="") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line or line == "END" or line.startswith("NESDUMP") or line.startswith("NESROM"):
                continue
            m = re.match(r"^\[(\w+)\](?: ([0-9A-F]{4})-([0-9A-F]{4}))?$", line)
            if m:
                section = m.group(1)
                if section in HEX_SECTIONS:
                    out[section] = {}
                continue
            if re.match(r"^[0-9A-F]{4}:", line):
                addr_s, _, rest = line.partition(":")
                base = int(addr_s, 16)
                for i, b in enumerate(rest.split()):
                    out[section][base + i] = int(b, 16)
                continue
            if section is None or line.startswith(("event:", "frame:", "scanline:", "pc_trigger:")):
                k, _, v = line.partition(":")
                out["header"][k.strip()] = v.strip()
                continue
            if section in ("CPU", "PPU", "BUS"):
                for tok in line.split():
                    k, _, v = tok.partition("=")
                    out[section][k] = v
                continue
            if "=" in line:  # PRG_HASH=... / CHR_HASH=... / INSN_HASH=... / INSN_COUNT=...
                k, _, v = line.partition("=")
                out["CRC"][k] = v
    return out


def diff_scalars(a, b):
    keys = sorted(set(a) | set(b), key=lambda k: list(a).index(k) if k in a else 999)
    return [(k, a.get(k, "<missing>"), b.get(k, "<missing>")) for k in keys if a.get(k) != b.get(k)]


def diff_bytes(a, b):
    addrs = sorted(set(a) | set(b))
    return [(x, a.get(x), b.get(x)) for x in addrs if a.get(x) != b.get(x)]


def fmt_byte(v):
    return "--" if v is None else "%02X" % v


def fmt_addr(sect, x):
    if sect == "FB":
        return "y=%d,x=%d" % (x >> 8, x & 0xFF)
    return "%04X" % x


def compare_event(pa, pb, wanted):
    """Return (report_lines, keys); keys are 'CPU.PC', 'RAM', ... that differ. Empty if identical."""
    A, B = parse_dump(pa), parse_dump(pb)
    lines, keys = [], []
    for sect in ("header", "CPU", "PPU", "BUS", "CRC"):
        if wanted and sect not in wanted and sect != "header":
            continue
        for k, va, vb in diff_scalars(A[sect], B[sect]):
            lines.append("    %-6s %-12s A=%s  B=%s" % (sect, k, va, vb))
            keys.append("%s.%s" % (sect, k))
    for sect in HEX_SECTIONS:
        if wanted and sect not in wanted:
            continue
        d = diff_bytes(A.get(sect, {}), B.get(sect, {}))
        if not d:
            continue
        shown = ", ".join("%s: %s/%s" % (fmt_addr(sect, x), fmt_byte(va), fmt_byte(vb)) for x, va, vb in d[:12])
        more = "" if len(d) <= 12 else "  ... +%d more" % (len(d) - 12)
        lines.append("    %-6s %d byte(s) differ (A/B): %s%s" % (sect, len(d), shown, more))
        keys.append(sect)
    return lines, keys


def read_events(d):
    """Chronological list of event filenames from events.txt (falls back to sorted dir listing)."""
    p = os.path.join(d, "events.txt")
    if os.path.exists(p):
        with open(p) as f:
            return [ln.split()[1] for ln in f if ln.strip()]
    return sorted(n for n in os.listdir(d) if re.match(r"(frame|scanline|pc)_.*\.txt$", n))


def read_run(d):
    p = os.path.join(d, "run.txt")
    info = {}
    if os.path.exists(p):
        with open(p) as f:
            for ln in f:
                k, _, v = ln.partition(":")
                info[k.strip()] = v.strip()
    return info


DIGEST_FIELDS = ("CPU", "PPU", "BUS", "APU", "RAM", "NT", "PAL", "OAM", "CHR", "PRG", "FB", "INSN")


def read_digest(d):
    """List of (name, {field: value, ...}) in file order."""
    out = []
    with open(os.path.join(d, "digest.txt")) as f:
        for ln in f:
            parts = ln.split()
            if len(parts) < 3:
                continue
            kv = dict(p.split("=", 1) for p in parts[2:] if "=" in p)
            out.append((parts[1], kv))
    return out


def compare_digests(args):
    A, B = read_digest(args.dir_a), read_digest(args.dir_b)
    print("digest events: A=%d B=%d" % (len(A), len(B)))
    status = 0
    first = None
    by_field = {}
    n = min(len(A), len(B))
    for i in range(n):
        na, ka = A[i]
        nb, kb = B[i]
        if na != nb:
            print("event sequence differs at #%d: A=%s B=%s" % (i + 1, na, nb))
            return 1
        if ka.get("ALL") == kb.get("ALL"):
            continue
        fields = [k for k in DIGEST_FIELDS if ka.get(k) != kb.get(k)]
        if first is None:
            first = (i, na, ka, kb, fields)
        for k in fields:
            cnt, fe = by_field.get(k, (0, na))
            by_field[k] = (cnt + 1, fe)
    if len(A) != len(B):
        print("event counts differ")
        status = 1
    if first is None:
        if status == 0:
            print("IDENTICAL (digest)")
        return status
    i, name, ka, kb, fields = first
    print("FIRST DIVERGENCE: #%d %s  (frame %s scanline %s)" % (i + 1, name, ka.get("f"), ka.get("sl")))
    print("  differing fields: %s" % ", ".join(fields))
    print("  A: pc=%s cyc=%s   B: pc=%s cyc=%s" % (ka.get("pc"), ka.get("cyc"), kb.get("pc"), kb.get("cyc")))
    print("\nSUMMARY (field -> differing events, first event):")
    for k, (cnt, fe) in sorted(by_field.items(), key=lambda kv: -kv[1][0]):
        print("  %-6s %7d  first: %s" % (k, cnt, fe))
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir_a")
    ap.add_argument("dir_b")
    ap.add_argument("--max", type=int, default=5, help="max differing events to print in detail (default 5)")
    ap.add_argument("--all", action="store_true", help="print every differing event")
    ap.add_argument("--summary", action="store_true",
                    help="also print, per differing field/section, how many events differ and the first one")
    ap.add_argument("--sections", default="", help="only compare these sections, e.g. CPU,PPU,RAM")
    args = ap.parse_args()

    for d in (args.dir_a, args.dir_b):
        if not os.path.isdir(d):
            print("error: not a directory: %s" % d, file=sys.stderr)
            return 2
    wanted = {s.strip().upper() for s in args.sections.split(",") if s.strip()}

    if os.path.exists(os.path.join(args.dir_a, "digest.txt")) and os.path.exists(os.path.join(args.dir_b, "digest.txt")):
        print("A: %s" % args.dir_a)
        print("B: %s" % args.dir_b)
        return compare_digests(args)

    ra, rb = read_run(args.dir_a), read_run(args.dir_b)
    print("A: %s (%s)" % (args.dir_a, ra.get("emulator", "?")))
    print("B: %s (%s)" % (args.dir_b, rb.get("emulator", "?")))

    status = 0
    rom_a, rom_b = os.path.join(args.dir_a, "rom.txt"), os.path.join(args.dir_b, "rom.txt")
    if os.path.exists(rom_a) and os.path.exists(rom_b):
        with open(rom_a) as fa, open(rom_b) as fb:
            if fa.read() != fb.read():
                print("ROM: rom.txt DIFFERS")
                status = 1

    ev_a, ev_b = read_events(args.dir_a), read_events(args.dir_b)
    set_a, set_b = set(ev_a), set(ev_b)
    only_a = [n for n in ev_a if n not in set_b]
    only_b = [n for n in ev_b if n not in set_a]
    if only_a:
        print("only in A (%d): %s%s" % (len(only_a), ", ".join(only_a[:8]), " ..." if len(only_a) > 8 else ""))
        status = 1
    if only_b:
        print("only in B (%d): %s%s" % (len(only_b), ", ".join(only_b[:8]), " ..." if len(only_b) > 8 else ""))
        status = 1

    common = [n for n in ev_a if n in set_b]
    differing = []
    by_key = {}
    for name in common:
        lines, keys = compare_event(os.path.join(args.dir_a, name), os.path.join(args.dir_b, name), wanted)
        if lines:
            differing.append((name, lines))
            for k in keys:
                cnt, first = by_key.get(k, (0, name))
                by_key[k] = (cnt + 1, first)

    print("events: A=%d B=%d common=%d identical=%d differing=%d"
          % (len(ev_a), len(ev_b), len(common), len(common) - len(differing), len(differing)))
    if differing:
        status = 1
        limit = len(differing) if args.all else args.max
        print("\nFIRST DIVERGENCE: %s" % differing[0][0])
        for name, lines in differing[:limit]:
            print("\n  %s" % name)
            print("\n".join(lines))
        if len(differing) > limit:
            print("\n  ... %d more differing event(s); use --all or --max N" % (len(differing) - limit))
        if args.summary:
            print("\nSUMMARY (field -> differing events, first event):")
            for k, (cnt, first) in sorted(by_key.items(), key=lambda kv: -kv[1][0]):
                print("  %-14s %6d  first: %s" % (k, cnt, first))
    elif status == 0:
        print("IDENTICAL")
    return status


if __name__ == "__main__":
    sys.exit(main())
