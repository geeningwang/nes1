#!/usr/bin/env python3
"""Generate one probe ROM per 6502 opcode ("opcode sweep") for CPU parity checks.

    python make_opcode_sweep.py <outdir> [--variants N] [--only HEX[,HEX...]] [--io]

--io aims the 3-byte opcodes at the PPU/APU/work-RAM/ROM address ranges instead of RAM.

Each ROM runs N variants.  A variant loads pseudo-random A/X/Y/P/S, executes the opcode under test with
pseudo-random operands, and then calls a checksum routine over $0000-$03FF.  The register values and the
stack pointer are visible in the instruction trace right after the opcode, the memory effects through
the checksum, so comparing the two emulators' traces and hashes covers the opcode completely.

Control-flow opcodes (BRK, JSR, RTI, RTS, JMP, branches) are not swept; they are covered by nestest and
the other probes.  KIL opcodes get one variant (the CPU stops there).  X is always even and indirect
pointers are even so that every effective address stays inside the 2 KB of RAM.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_probe_rom import build_ines, vectors, PREAMBLE  # noqa: E402

SKIP = {0x00, 0x20, 0x40, 0x60, 0x4C, 0x6C} | {0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0}
KIL = {0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x92, 0xB2, 0xD2, 0xF2}
IMM = {0x80, 0x82, 0x89, 0xC2, 0xE2, 0xA0, 0xA2, 0xC0, 0xE0} | {o for o in range(256) if o & 0x1F in (0x09, 0x0B)}


def length(op):
    if op in KIL:
        return 1
    if op in IMM:
        return 2
    low = op & 0x1F
    if low in (0x08, 0x0A, 0x18, 0x1A):
        return 1
    if low in (0x0C, 0x0D, 0x0E, 0x0F, 0x19, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F):
        return 3
    return 2


SPECIAL = [0x00, 0x01, 0x7F, 0x80, 0xFF, 0x55, 0xAA]


def pick(rng):
    return rng.choice(SPECIAL) if rng.random() < 0.4 else rng.randrange(256)


def program(op, variants, io=False):
    rng = random.Random(0x6502 + op + (0x100 if io else 0))
    n = length(op)
    lines = [PREAMBLE.rstrip("\n")]
    # zero page: even offsets random, odd offsets a small high byte, so (zp) pointers stay in RAM
    for i in range(256):
        lines.append("    LDA #$%02X\n    STA $%02X" % (rng.randrange(256) if i % 2 == 0 else rng.choice([2, 3]), i))
    for page in (2, 3):
        for i in range(256):
            lines.append("    LDA #$%02X\n    STA $%02X%02X" % (rng.randrange(256), page, i))
    for v in range(1 if op in KIL else variants):
        s = rng.choice([0xFF, 0xFF, 0x80, 0x01, 0x00, 0xFE, 0xA0])
        p = (rng.randrange(256) | 0x04) & 0xFF
        a, x, y = pick(rng), pick(rng) & 0xFE, pick(rng)
        if io:
            x, y = rng.randrange(0, 8) & 0xFE, rng.randrange(0, 8)
        if n == 2:
            b1 = pick(rng) if op in IMM else (rng.randrange(256) & 0xFE)
            raw = [op, b1]
        elif n == 3:
            b1, b2 = rng.choice([(0x34, 3), (0xFF, 3), (0x00, 3), (0xF0, 2), (0xAA, 3), (0x00, 2)])
            if io:   # operands in the register areas: PPU, APU/IO, work RAM, ROM
                b1, b2 = rng.choice([(0, 0x20), (1, 0x20), (3, 0x20), (4, 0x20), (5, 0x20), (6, 0x20), (7, 0x20), (2, 0x20),
                                     (0x15, 0x40), (0x17, 0x40), (0x16, 0x40), (0x00, 0x40), (0x10, 0x40), (0x14, 0x40),
                                     (0x00, 0x50), (0x00, 0x60), (0xFF, 0x7F), (0x00, 0x80), (0x10, 0x30), (0x08, 0x08)])
            raw = [op, b1, b2]
        else:
            raw = [op]
        lines.append("    LDX #$%02X\n    TXS\n    LDA #$%02X\n    PHA\n    LDA #$%02X\n    LDX #$%02X\n    LDY #$%02X\n    PLP"
                     % (s, p, a, x, y))
        lines.append("    .byte " + ",".join("$%02X" % b for b in raw))
        lines.append("    JSR chk")
    lines.append("done:\n    JMP done")
    lines.append("""chk:
    LDX #0
    LDA #0
c1:
    EOR $0000,X
    ROL A
    EOR $0100,X
    ROL A
    EOR $0200,X
    ROL A
    EOR $0300,X
    ROL A
    INX
    BNE c1
    RTS
nmi:
    RTI
irq:
    RTI
""")
    return "\n".join(lines) + vectors()


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    outdir = args[0]
    variants = 20
    io = "--io" in args
    only = None
    if "--variants" in args:
        variants = int(args[args.index("--variants") + 1])
    if "--only" in args:
        only = {int(t, 16) for t in args[args.index("--only") + 1].split(",")}
    os.makedirs(outdir, exist_ok=True)
    count = 0
    for op in range(256):
        if op in SKIP or (only and op not in only):
            continue
        with open(os.path.join(outdir, "op_%02X.nes" % op), "wb") as f:
            f.write(build_ines(program(op, variants, io)))
        count += 1
    print("wrote %d ROMs to %s" % (count, outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
