#!/usr/bin/env python3
"""Generate small NROM test ("probe") ROMs, each exercising one behaviour.

    python make_probe_rom.py [outdir]        # default: <repo>/test/probe

The ROMs are written from the programs in PROBES below with a minimal 6502 assembler, so they are our
own work (no third-party licence).  Each probe runs forever after finishing, writing its observations
into RAM ($0300 and up) and/or leaving the PPU in a state that the state dump can observe.

Assembler syntax: one statement per line, ';' comments, 'label:' lines, '.org ADDR', '.byte a,b,..',
'.word a,..', '#imm', '$hex', '%bin', decimal, 'label', 'label+n', '<label' / '>label' (low/high byte).
"""
import os
import re
import sys

# mnemonic -> {mode: opcode}
OPS = {
    "ADC": {"imm": 0x69, "zp": 0x65, "zpx": 0x75, "abs": 0x6D, "abx": 0x7D, "aby": 0x79, "inx": 0x61, "iny": 0x71},
    "AND": {"imm": 0x29, "zp": 0x25, "zpx": 0x35, "abs": 0x2D, "abx": 0x3D, "aby": 0x39, "inx": 0x21, "iny": 0x31},
    "ASL": {"acc": 0x0A, "zp": 0x06, "zpx": 0x16, "abs": 0x0E, "abx": 0x1E},
    "BIT": {"zp": 0x24, "abs": 0x2C},
    "CMP": {"imm": 0xC9, "zp": 0xC5, "zpx": 0xD5, "abs": 0xCD, "abx": 0xDD, "aby": 0xD9, "inx": 0xC1, "iny": 0xD1},
    "CPX": {"imm": 0xE0, "zp": 0xE4, "abs": 0xEC},
    "CPY": {"imm": 0xC0, "zp": 0xC4, "abs": 0xCC},
    "DEC": {"zp": 0xC6, "zpx": 0xD6, "abs": 0xCE, "abx": 0xDE},
    "EOR": {"imm": 0x49, "zp": 0x45, "zpx": 0x55, "abs": 0x4D, "abx": 0x5D, "aby": 0x59, "inx": 0x41, "iny": 0x51},
    "INC": {"zp": 0xE6, "zpx": 0xF6, "abs": 0xEE, "abx": 0xFE},
    "JMP": {"abs": 0x4C, "ind": 0x6C},
    "JSR": {"abs": 0x20},
    "LDA": {"imm": 0xA9, "zp": 0xA5, "zpx": 0xB5, "abs": 0xAD, "abx": 0xBD, "aby": 0xB9, "inx": 0xA1, "iny": 0xB1},
    "LDX": {"imm": 0xA2, "zp": 0xA6, "zpy": 0xB6, "abs": 0xAE, "aby": 0xBE},
    "LDY": {"imm": 0xA0, "zp": 0xA4, "zpx": 0xB4, "abs": 0xAC, "abx": 0xBC},
    "LSR": {"acc": 0x4A, "zp": 0x46, "zpx": 0x56, "abs": 0x4E, "abx": 0x5E},
    "ORA": {"imm": 0x09, "zp": 0x05, "zpx": 0x15, "abs": 0x0D, "abx": 0x1D, "aby": 0x19, "inx": 0x01, "iny": 0x11},
    "ROL": {"acc": 0x2A, "zp": 0x26, "zpx": 0x36, "abs": 0x2E, "abx": 0x3E},
    "ROR": {"acc": 0x6A, "zp": 0x66, "zpx": 0x76, "abs": 0x6E, "abx": 0x7E},
    "SBC": {"imm": 0xE9, "zp": 0xE5, "zpx": 0xF5, "abs": 0xED, "abx": 0xFD, "aby": 0xF9, "inx": 0xE1, "iny": 0xF1},
    "STA": {"zp": 0x85, "zpx": 0x95, "abs": 0x8D, "abx": 0x9D, "aby": 0x99, "inx": 0x81, "iny": 0x91},
    "STX": {"zp": 0x86, "zpy": 0x96, "abs": 0x8E},
    "STY": {"zp": 0x84, "zpx": 0x94, "abs": 0x8C},
}
IMPLIED = {"BRK": 0x00, "CLC": 0x18, "CLD": 0xD8, "CLI": 0x58, "CLV": 0xB8, "DEX": 0xCA, "DEY": 0x88, "INX": 0xE8,
           "INY": 0xC8, "NOP": 0xEA, "PHA": 0x48, "PHP": 0x08, "PLA": 0x68, "PLP": 0x28, "RTI": 0x40, "RTS": 0x60,
           "SEC": 0x38, "SED": 0xF8, "SEI": 0x78, "TAX": 0xAA, "TAY": 0xA8, "TSX": 0xBA, "TXA": 0x8A, "TXS": 0x9A,
           "TYA": 0x98}
BRANCH = {"BCC": 0x90, "BCS": 0xB0, "BEQ": 0xF0, "BMI": 0x30, "BNE": 0xD0, "BPL": 0x10, "BVC": 0x50, "BVS": 0x70}


class AsmError(Exception):
    pass


def parse_num(tok, labels, pc, final):
    tok = tok.strip()
    if tok[:1] in "<>":
        v = parse_num(tok[1:], labels, pc, final)
        return (v & 0xFF) if tok[0] == "<" else ((v >> 8) & 0xFF)
    m = re.match(r"^([A-Za-z_]\w*)\s*([+-]\s*\d+)?$", tok)
    if m:
        if m.group(1) not in labels:
            if final:
                raise AsmError("unknown label %r" % m.group(1))
            return 0
        off = int(m.group(2).replace(" ", "")) if m.group(2) else 0
        return labels[m.group(1)] + off
    if tok.startswith("$"):
        return int(tok[1:], 16)
    if tok.startswith("%"):
        return int(tok[1:], 2)
    return int(tok, 0)


def operand(op, labels, pc, final):
    """Return (mode, value) for an operand string."""
    op = op.strip()
    if op.upper() == "A":
        return "acc", 0
    if op.startswith("#"):
        return "imm", parse_num(op[1:], labels, pc, final)
    m = re.match(r"^\((.+),\s*[xX]\)$", op)
    if m:
        return "inx", parse_num(m.group(1), labels, pc, final)
    m = re.match(r"^\((.+)\),\s*[yY]$", op)
    if m:
        return "iny", parse_num(m.group(1), labels, pc, final)
    m = re.match(r"^\((.+)\)$", op)
    if m:
        return "ind", parse_num(m.group(1), labels, pc, final)
    m = re.match(r"^(.+),\s*([xXyY])$", op)
    idx = m.group(2).lower() if m else ""
    v = parse_num(m.group(1) if m else op, labels, pc, final)
    zp = v < 0x100 and not re.match(r"^[A-Za-z_]", (m.group(1) if m else op).strip())
    base = "zp" if zp else "abs"
    return (base[:2] + idx if zp else "ab" + idx) if idx else base, v


def assemble(src, org=0x8000):
    """Two-pass assemble; returns bytes for PRG starting at 'org' (a '.org' later sets pc)."""
    lines = []
    for raw in src.splitlines():
        line = raw.split(";")[0].strip()
        if not line:
            continue
        while True:
            m = re.match(r"^([A-Za-z_]\w*):\s*(.*)$", line)
            if not m:
                break
            lines.append(("label", m.group(1)))
            line = m.group(2).strip()
        if line:
            lines.append(("stmt", line))

    labels = {}
    out = None
    for final in (False, True):
        pc = org
        out = {}
        for kind, text in lines:
            if kind == "label":
                labels[text] = pc
                continue
            parts = text.split(None, 1)
            mn = parts[0].upper()
            arg = parts[1] if len(parts) > 1 else ""
            if mn == ".ORG":
                pc = parse_num(arg, labels, pc, final)
                continue
            if mn in (".BYTE", ".WORD"):
                for tok in arg.split(","):
                    v = parse_num(tok, labels, pc, final)
                    if mn == ".BYTE":
                        out[pc] = v & 0xFF
                        pc += 1
                    else:
                        out[pc] = v & 0xFF
                        out[pc + 1] = (v >> 8) & 0xFF
                        pc += 2
                continue
            if mn in IMPLIED:
                out[pc] = IMPLIED[mn]
                pc += 1
            elif mn in BRANCH:
                target = parse_num(arg, labels, pc, final)
                rel = target - (pc + 2)
                if final and not -128 <= rel <= 127:
                    raise AsmError("branch out of range: %s" % text)
                out[pc] = BRANCH[mn]
                out[pc + 1] = rel & 0xFF
                pc += 2
            elif mn in OPS:
                mode, v = operand(arg, labels, pc, final)
                table = OPS[mn]
                if mode not in table and mode in ("zp", "zpx", "zpy"):
                    mode = "ab" + mode[2:] if mode != "zp" else "abs"
                if mode not in table:
                    raise AsmError("bad addressing mode %r for %s" % (mode, text))
                out[pc] = table[mode]
                if mode in ("acc",):
                    pc += 1
                elif mode in ("imm", "zp", "zpx", "zpy", "inx", "iny"):
                    out[pc + 1] = v & 0xFF
                    pc += 2
                else:
                    out[pc + 1] = v & 0xFF
                    out[pc + 2] = (v >> 8) & 0xFF
                    pc += 3
            else:
                raise AsmError("unknown statement %r" % text)
    return out


def build_ines(program, chr_data=None, mirroring=1):
    """32 KB PRG (NROM-256) + 8 KB CHR.  Vectors default to labels nmi/reset/irq if defined."""
    mem = assemble(program)
    prg = bytearray([0xFF] * 0x8000)
    for addr, b in mem.items():
        if addr < 0x8000:
            raise AsmError("code below $8000 at %04X" % addr)
        prg[addr - 0x8000] = b
    header = bytes([0x4E, 0x45, 0x53, 0x1A, 2, 1, mirroring & 1, 0]) + bytes(8)
    chr_rom = bytes(chr_data) if chr_data else bytes(0x2000)
    return header + bytes(prg) + chr_rom


def vectors(nmi="nmi", reset="reset", irq="irq"):
    return "\n.org $FFFA\n.word %s, %s, %s\n" % (nmi, reset, irq)


PREAMBLE = """
reset:
    SEI
    CLD
    LDX #$FF
    TXS
    LDA #0
    STA $2000
    STA $2001
"""
WAIT_VBL = """
wait_vbl_{n}:
    BIT $2002
    BPL wait_vbl_{n}
"""


def wait_vbl(n):
    return WAIT_VBL.format(n=n)


STUBS = "nmi:\n    RTI\nirq:\n    RTI\n"

# name -> (description, program).  Every program ends in a forever loop 'done: JMP done'.
PROBES = {
    # Reads $2002 in a tight loop after two vblank waits and logs 16 consecutive values to $0300.
    "ppu_status_poll": ("$2002 bit timing: logs consecutive reads after vblank", PREAMBLE + wait_vbl(1) + wait_vbl(2) + """
    LDX #0
log:
    LDA $2002
    STA $0300,X
    INX
    CPX #16
    BNE log
done:
    JMP done
""" + STUBS + vectors()),
    # Reads every PPU register address and logs the results (open-bus behaviour).
    "ppu_open_bus": ("reads of write-only PPU registers", PREAMBLE + """
    LDA #$5A
    STA $2000
    LDX #0
rd:
    LDA $2000,X
    STA $0300,X
    INX
    CPX #8
    BNE rd
done:
    JMP done
""" + STUBS + vectors()),
    # Turns background rendering on with a scroll set, then idles: V/T evolve every scanline.
    "ppu_scroll_idle": ("V/T evolution with rendering on", PREAMBLE + wait_vbl(1) + wait_vbl(2) + """
    LDA #$00
    STA $2006
    STA $2006
    LDA #$13
    STA $2005
    LDA #$25
    STA $2005
    LDA #$1E
    STA $2001
done:
    JMP done
""" + STUBS + vectors()),
    # Writes to ROM space and unmapped regions, then reads them back.
    "mem_map": ("writes to ROM / unmapped space and read-back", PREAMBLE + """
    LDA #$A5
    STA $8000
    STA $C000
    STA $5000
    STA $6000
    LDA $8000
    STA $0300
    LDA $C000
    STA $0301
    LDA $5000
    STA $0302
    LDA $6000
    STA $0303
    LDA $4018
    STA $0304
    LDA $4016
    STA $0305
    LDA $4017
    STA $0306
done:
    JMP done
""" + STUBS + vectors()),
    # Executes BRK/IRQ vector paths and PHP/PLP/RTI flag handling.
    "cpu_flags_brk": ("PHP/PLP/RTI/BRK status-flag behaviour", PREAMBLE + """
    LDA #$FF
    PHA
    PLP
    PHP
    PLA
    STA $0300
    LDA #$00
    PHA
    PLP
    PHP
    PLA
    STA $0301
    BRK
    .byte $00
    LDA #$11
    STA $0302
done:
    JMP done
nmi:
    RTI
irq:
    PHP
    PLA
    STA $0303
    RTI
""" + vectors()),
    # OAM DMA with a non-zero OAMADDR, then reads $2004 back.
    "oam_dma": ("OAM DMA cost and OAMADDR behaviour", PREAMBLE + wait_vbl(1) + """
    LDX #0
fill:
    TXA
    STA $0200,X
    INX
    BNE fill
    LDA #$10
    STA $2003
    LDA #2
    STA $4014
    LDA #$00
    STA $2003
    LDA $2004
    STA $0300
    LDA #$10
    STA $2003
    LDA $2004
    STA $0301
done:
    JMP done
""" + STUBS + vectors()),
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "test", "probe")
    os.makedirs(outdir, exist_ok=True)
    for name, (desc, prog) in sorted(PROBES.items()):
        data = build_ines(prog)
        path = os.path.join(outdir, name + ".nes")
        with open(path, "wb") as f:
            f.write(data)
        print("%-18s %6d bytes  %s" % (name + ".nes", len(data), desc))


if __name__ == "__main__":
    main()
