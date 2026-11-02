#!/usr/bin/env python3
"""Compare two NESDUMP output folders (e.g. nes1 vs FCEUX) and report where they differ.

    python3 tool/compare_dumps.py <dirA> <dirB> [--max N] [--all] [--sections CPU,PPU,RAM,...]

Both folders come from the same command line (see doc/state_dump.md), so they contain
the same event files.  Events are compared in the chronological order recorded in
events.txt.  For each differing event the report lists the differing CPU/PPU fields
and, for memory sections, the exact addresses and byte values that differ.

Only the current format (NESDUMP 3 / NESDIGEST 3) is accepted. A dump with another version, a
missing or truncated section, a missing END line, or a manifest (run.txt) that disagrees with the
event files is an error, never "identical".

Exit status: 0 if the dumps are identical, 1 if they differ, 2 on usage, IO or format errors.
"""
import argparse
import itertools
import os
import re
import sys

DUMP_VERSION = "NESDUMP 3"
DIGEST_VERSION = "NESDIGEST 3"

# section -> number of bytes it must contain
HEX_SIZES = {"APU": 0x18, "APUX": 0x20, "RAM": 0x800, "WRAM": 0x2000, "NT": 0x800, "NTV": 0x1000,
             "PAL": 0x20, "OAM": 0x100, "CHR": 0x2000, "FB": 240 * 256, "IO": 0x10, "IRQ": 8}
HEX_SECTIONS = tuple(HEX_SIZES) + ("PRG",)   # PRG is only present in dumps made with --rom-every
SCALAR_SECTIONS = ("CPU", "PPU", "BUS")
CRC_KEYS = ("PRG_HASH", "CHR_HASH", "INSN_HASH", "INSN_COUNT")


class DumpFormatError(ValueError):
    pass


def parse_dump(path):
    """Return {'header': {k: v}, 'CPU': {k: v}, 'PPU': {k: v}, 'APU'..: {addr: byte}, 'CRC': {k: v}}."""
    out = {"header": {}, "CPU": {}, "PPU": {}, "BUS": {}, "CRC": {}}
    section = None
    first = True
    ended = False
    with open(path, "r", newline="") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if first:
                first = False
                if line != DUMP_VERSION:
                    raise DumpFormatError("%s: expected %r, found %r" % (path, DUMP_VERSION, line[:40]))
                continue
            if line == "END":
                ended = True
                continue
            if not line or line.startswith("NESROM"):
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
    if first or not ended:
        raise DumpFormatError("%s: truncated dump (no END line)" % path)
    for name in SCALAR_SECTIONS:
        if not out[name]:
            raise DumpFormatError("%s: missing section [%s]" % (path, name))
    for name, size in HEX_SIZES.items():
        if name not in out:
            raise DumpFormatError("%s: missing section [%s]" % (path, name))
        if len(out[name]) != size:
            raise DumpFormatError("%s: section [%s] has %d bytes, expected %d" % (path, name, len(out[name]), size))
    for key in CRC_KEYS:
        if key not in out["CRC"]:
            raise DumpFormatError("%s: missing %s" % (path, key))
    for key in ("event", "frame", "scanline"):
        if key not in out["header"]:
            raise DumpFormatError("%s: missing header field %s" % (path, key))
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


DIGEST_FIELDS = ("CPU", "PPU", "BUS", "APU", "RAM", "WRAM", "NT", "NTV", "PAL", "OAM", "CHR", "PRG", "FB",
                 "IO", "IRQ", "INSN")
DIGEST_REQUIRED = ("ALL", "f", "sl", "pc", "cyc") + DIGEST_FIELDS


def iter_digest(d):
    """Yield (name, {field: value, ...}) in file order, one line at a time (digests can be very large)."""
    path = os.path.join(d, "digest.txt")
    with open(path) as f:
        if f.readline().rstrip("\n") != DIGEST_VERSION:
            raise DumpFormatError("%s: expected %r on the first line" % (path, DIGEST_VERSION))
        for number, ln in enumerate(f, 2):
            parts = ln.split()
            kv = dict(p.split("=", 1) for p in parts[2:] if "=" in p)
            missing = [k for k in DIGEST_REQUIRED if k not in kv]
            if len(parts) < 3 or missing:
                raise DumpFormatError("%s:%d: malformed digest line (missing %s)" % (path, number, ", ".join(missing) or "fields"))
            yield parts[1], kv


def read_digest(d):
    """List of (name, {field: value, ...}) in file order."""
    return list(iter_digest(d))


def check_manifest(d, kind, events):
    """The run.txt manifest must name the current format and agree with the number of events on disk."""
    info = read_run(d)
    want = DIGEST_VERSION if kind == "digest" else DUMP_VERSION
    if info.get("format") != want:
        raise DumpFormatError("%s: manifest format is %r, expected %r" % (d, info.get("format"), want))
    m = re.match(r"^frame=(\d+) scanline=(\d+) pc=(\d+) insn=(\d+)$", info.get("events", ""))
    if not m:
        raise DumpFormatError("%s: manifest has no event counts" % d)
    if sum(int(x) for x in m.groups()) != events:
        raise DumpFormatError("%s: manifest records %s events but %d are present" % (d, info["events"], events))
    return info


def compare_digests(args):
    status = 0
    first = None
    by_field = {}
    na_count = nb_count = 0
    for i, (ea, eb) in enumerate(itertools.zip_longest(iter_digest(args.dir_a), iter_digest(args.dir_b))):
        na_count += ea is not None
        nb_count += eb is not None
        if ea is None or eb is None:
            continue
        (na, ka), (nb, kb) = ea, eb
        if na != nb:
            print("event sequence differs at #%d: A=%s B=%s" % (i + 1, na, nb))
            return 1
        if ka["ALL"] == kb["ALL"]:
            continue
        fields = [k for k in DIGEST_FIELDS if ka.get(k) != kb.get(k)]
        if first is None:
            first = (i, na, ka, kb, fields)
        for k in fields:
            cnt, fe = by_field.get(k, (0, na))
            by_field[k] = (cnt + 1, fe)
    print("digest events: A=%d B=%d" % (na_count, nb_count))
    ia = check_manifest(args.dir_a, "digest", na_count)
    ib = check_manifest(args.dir_b, "digest", nb_count)
    if na_count != nb_count or ia.get("frames_run") != ib.get("frames_run"):
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


def main(argv=None):
    try:
        return run(argv)
    except (DumpFormatError, OSError) as err:
        print("ERROR: %s" % err, file=sys.stderr)
        return 2


def run(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir_a")
    ap.add_argument("dir_b")
    ap.add_argument("--max", type=int, default=5, help="max differing events to print in detail (default 5)")
    ap.add_argument("--all", action="store_true", help="print every differing event")
    ap.add_argument("--summary", action="store_true",
                    help="also print, per differing field/section, how many events differ and the first one")
    ap.add_argument("--sections", default="", help="only compare these sections, e.g. CPU,PPU,RAM")
    args = ap.parse_args(argv)

    for d in (args.dir_a, args.dir_b):
        if not os.path.isdir(d):
            print("error: not a directory: %s" % d, file=sys.stderr)
            return 2
    wanted = {s.strip().upper() for s in args.sections.split(",") if s.strip()}

    has_digest = [os.path.exists(os.path.join(d, "digest.txt")) for d in (args.dir_a, args.dir_b)]
    if has_digest[0] != has_digest[1]:
        raise DumpFormatError("one folder holds a digest and the other full dumps")
    if all(has_digest):
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
    for d, ev in ((args.dir_a, ev_a), (args.dir_b, ev_b)):
        if not os.path.exists(os.path.join(d, "events.txt")):
            raise DumpFormatError("%s: events.txt is missing" % d)
        check_manifest(d, "dump", len(ev))
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
