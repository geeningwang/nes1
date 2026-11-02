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
    SEI
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
    # Every NOP variant, to observe what each leaves on the data bus (read the DB column of the trace).
    "cpu_nop_modes": ("unofficial NOP addressing modes and the data bus", PREAMBLE + """
    LDX #0
    LDY #0
    NOP
    .byte $04, $04
    NOP
    .byte $44, $04
    NOP
    .byte $64, $04
    NOP
    .byte $0C, $04, $00
    NOP
    .byte $14, $04
    NOP
    .byte $34, $04
    NOP
    .byte $54, $04
    NOP
    .byte $74, $04
    NOP
    .byte $D4, $04
    NOP
    .byte $F4, $04
    NOP
    .byte $1C, $04, $00
    NOP
    .byte $3C, $04, $00
    NOP
    .byte $5C, $04, $00
    NOP
    .byte $7C, $04, $00
    NOP
    .byte $DC, $04, $00
    NOP
    .byte $FC, $04, $00
    NOP
    .byte $80, $04
    NOP
    .byte $82, $04
    NOP
    .byte $89, $04
    NOP
    .byte $C2, $04
    NOP
    .byte $E2, $04
    NOP
    .byte $1A
    NOP
    .byte $3A
    NOP
    .byte $5A
    NOP
    .byte $7A
    NOP
    .byte $DA
    NOP
    .byte $FA
    NOP
done:
    JMP done
""" + STUBS + vectors()),
    # Length counter of pulse 1 (length 2): $4015 read about every 1300 cycles, logged to $0300.
    "apu_len_status": ("$4015 length status decays with the frame counter", PREAMBLE + """
    LDA #$01
    STA $4015
    LDA #$18
    STA $4003
    LDX #0
rd:
    LDA $4015
    STA $0300,X
    LDY #$FF
dl:
    DEY
    BNE dl
    INX
    CPX #40
    BNE rd
done:
    JMP done
""" + "nmi:\n    RTI\nirq:\n    RTI\n" + vectors()),
    # Frame-counter IRQ (enabled by default): I flag cleared, handler acknowledges through $4015.
    "apu_frame_irq": ("APU frame IRQ timing", PREAMBLE + """
    CLI
spin:
    NOP
    JMP spin
nmi:
    RTI
irq:
    PHA
    LDA $4015
    STA $0301
    INC $0300
    PLA
    RTI
""" + vectors()),
    # Frame IRQ inhibited, then 5-step mode.
    "apu_frame_mode": ("$4017 inhibit and 5-step mode", PREAMBLE + """
    LDA #$40
    STA $4017
    CLI
    LDY #0
w1:
    LDX #0
w2:
    DEX
    BNE w2
    DEY
    BNE w1
    LDA #$80
    STA $4017
    LDA $4015
    STA $0302
spin:
    JMP spin
nmi:
    RTI
irq:
    INC $0300
    LDA $4015
    STA $0301
    RTI
""" + vectors()),
    # DMC: 17 bytes at the slowest rate with its IRQ enabled.
    "apu_dmc_irq": ("DMC sample, DMA stalls and DMC IRQ", PREAMBLE + """
    LDA #$80
    STA $4010
    LDA #$00
    STA $4012
    LDA #$01
    STA $4013
    LDA #$10
    STA $4015
    LDA $4015
    STA $0302
    CLI
spin:
    NOP
    JMP spin
nmi:
    RTI
irq:
    LDA $4015
    STA $0301
    LDA #$00
    STA $4010
    INC $0300
    RTI
""" + vectors()),
    # Pseudo-random APU register writes at pseudo-random times, with the IRQ line live.  Checks the
    # frame counter, length counters, DMC and the I-flag latency together; the dump's APU/APUX state
    # and the instruction chain hash expose any difference.  SEED picks the sequence.
    "apu_fuzz": ("random APU register traffic with IRQs", PREAMBLE + """
    LDA #$A5
    STA $00
    LDA #$3C
    STA $01
    CLI
loop:
    JSR rnd
    AND #$0F
    TAY
    LDX regtab,Y
    JSR rnd
    AND andtab,Y
    STA $4000,X
    JSR rnd
    AND #$3F
    TAY
dl:
    DEY
    BNE dl
    JSR rnd
    AND #$03
    BNE skip1
    LDA $4015
    EOR $02
    STA $02
skip1:
    JSR rnd
    AND #$07
    BNE loop
    PHP
    PLA
    EOR #$04
    PHA
    PLP
    JMP loop
rnd:
    INC $01
    LDA $00
    ASL A
    BCC r1
    EOR #$1D
r1:
    STA $00
    EOR $01
    RTS
regtab:
    .byte $00,$03,$08,$0B,$0C,$0F,$10,$11,$12,$13,$15,$15,$17,$17,$10,$13
andtab:
    .byte $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$BF,$BF,$FF,$FF
nmi:
    RTI
irq:
    PHA
    LDA $4015
    EOR $04
    STA $04
    INC $03
    PLA
    RTI
""" + vectors()),
    # A KIL (JAM) opcode: observes what the CPU does when it halts.
    "cpu_kil": ("KIL/JAM opcode $02", PREAMBLE + """
    LDA #$77
    STA $0300
    .byte $02
    LDA #$88
    STA $0301
done:
    JMP done
""" + STUBS + vectors()),
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
    STA $0400
    LDA #$10
    STA $2003
    LDA $2004
    STA $0401
    LDX #0
fill3:
    TXA
    ORA #$80
    STA $0300,X
    INX
    BNE fill3
    LDA #3
    STA $4014
    LDA #$FD
    STA $2003
    LDA #$A1
    STA $2004
    STA $2004
    STA $2004
    STA $2004
done:
    JMP done
""" + STUBS + vectors()),
}


def _mem_extent():
    """Writes a distinct value to addresses all over $4018-$7FFF, then reads them all back into $0300+."""
    addrs = [0x4018, 0x4019, 0x401F, 0x4020, 0x4100, 0x4FFF, 0x5000, 0x5555, 0x5FFF, 0x6000, 0x6001, 0x60FF,
             0x6100, 0x6400, 0x67FF, 0x6800, 0x6801, 0x6FFF, 0x7000, 0x7001, 0x77FF, 0x7800, 0x7BFF, 0x7C00,
             0x7FFE, 0x7FFF, 0x8000, 0xFFF0]
    w = []
    r = []
    for i, a in enumerate(addrs):
        w.append("    LDA #$%02X\n    STA $%04X" % ((0x21 * (i + 1) + 7) & 0xFF, a))
    for i, a in enumerate(addrs):
        r.append("    LDA $%04X\n    STA $%04X" % (a, 0x0300 + i))
    return PREAMBLE + "\n".join(w) + "\n" + "\n".join(r) + "\ndone:\n    JMP done\n" + STUBS + vectors()


PROBES["mem_extent"] = ("work RAM extent and unmapped space between $4018 and $7FFF", _mem_extent())


def _sprite_stress():
    """64 moving sprites over a random background; 8x8/8x16 size and the left-column masks change over time."""
    import random
    rng = random.Random(0x5EED)
    pal = ",".join("$%02X" % rng.randrange(0x40) for _ in range(32))
    prog = PREAMBLE + wait_vbl(1) + wait_vbl(2) + """
    LDA #$A7
    STA $10
    LDA #$3C
    STA $11
    LDA #$3F
    STA $2006
    LDA #0
    STA $2006
    LDX #0
pl:
    LDA paltab,X
    STA $2007
    INX
    CPX #32
    BNE pl
    LDA #$20
    STA $2006
    LDA #0
    STA $2006
    LDX #0
    LDY #4
nt:
    JSR rnd
    STA $2007
    INX
    BNE nt
    DEY
    BNE nt
    LDX #0
oam:
    JSR rnd
    STA $0200,X
    INX
    BNE oam
    LDX #0
clus:
    LDA $0200,X
    AND #$3F
    CLC
    ADC #$20
    STA $0200,X
    INX
    INX
    INX
    INX
    CPX #128
    BNE clus
    LDA #0
    STA $00
    LDA $2002
    LDA #$80
    STA $2000
    LDA #$1E
    STA $2001
spin:
    JMP spin
rnd:
    INC $11
    LDA $10
    ASL A
    BCC r1
    EOR #$1D
r1:
    STA $10
    EOR $11
    RTS
paltab:
    .byte """ + pal + """
nmi:
    PHA
    TXA
    PHA
    LDA #$02
    STA $4014
    INC $00
    LDA $00
    AND #$28
    ORA #$80
    STA $2000
    LDA $00
    LSR A
    LSR A
    LSR A
    LSR A
    LSR A
    LSR A
    AND #$03
    ASL A
    ORA #$18
    STA $2001
    LDA $2002
    LDA $00
    STA $2005
    LDA #0
    STA $2005
    LDX #0
mv:
    INC $0200,X
    LDA $0203,X
    CLC
    ADC #3
    STA $0203,X
    INX
    INX
    INX
    INX
    BNE mv
    PLA
    TAX
    PLA
    RTI
irq:
    RTI
""" + vectors()
    chr_data = bytes((rng.randrange(256) & rng.randrange(256)) for _ in range(0x2000))
    return prog, chr_data


_prog, _chr = _sprite_stress()
PROBES["ppu_sprite_stress"] = ("moving sprites, 8x8/8x16, left-column masks", _prog, _chr)


# Turns rendering off at pseudo-random positions in the visible lines.  Whether the pending Y increment of
# V (spec O-21) has already happened at that moment depends on the exact catch-up threshold, so V at the end
# of such a line pins the threshold down to one dot.
PROBES["ppu_fixit_threshold"] = ("rendering off at varying positions: Y-increment threshold", PREAMBLE + """
    LDA #0
    STA $00
loop:
    LDA #$1E
    STA $2001
    LDX $00
    INX
    CPX #37
    BNE ok
    LDX #1
ok:
    STX $00
d1:
    DEX
    BNE d1
    LDA #0
    STA $2001
    NOP
    NOP
    JMP loop
""" + STUBS + vectors())


PROBES["controller_serial"] = ("controller serial reports, held strobe, 256-read wrap", PREAMBLE + """
loop:
    LDA #1
    STA $4016
    LDA $4016
    STA $0300
    LDA $4016
    STA $0301
    LDA #0
    STA $4016
    LDX #0
rd:
    LDA $4016
    STA $0400,X
    LDA $4017
    STA $0500,X
    INX
    BNE rd
    LDA $4016
    STA $0302
    LDA $4017
    STA $0303
    JMP loop
""" + STUBS + vectors())


PROBES["ppu_bg_toggle_fine_x"] = (
    "background toggles with fine X: output advances in eight-pixel chunks",
    PREAMBLE + wait_vbl(1) + wait_vbl(2) + """
    LDA #$3F
    STA $2006
    LDA #0
    STA $2006
    LDA #$0F
    STA $2007
    LDA #$36
    STA $2007
    LDA $2002
    LDA #0
    STA $2006
    STA $2006
    LDA #3
    STA $2005
    LDA #0
    STA $2005
loop:
    LDA #$0A
    STA $2001
    LDX #5
delay:
    DEX
    BNE delay
    LDA #0
    STA $2001
    JMP loop
""" + STUBS + vectors(),
    bytes([0xFF] * 8 + [0] * 8) * 512,
)


# Leaves a recognisable machine state, then reports what a reset or power cycle did to it. The first boot
# (no magic in RAM) arms the state and spins; a boot that finds the magic is a soft reset and records the
# retained A/X/Y/S/P, then tries to load a length counter without enabling the channel.
PROBES["reset_state"] = ("state kept or cleared by a soft reset and a power cycle", """
reset:
    STA $0700
    STX $0701
    STY $0702
    TSX
    STX $0703
    PHP
    PLA
    STA $0704
    SEI
    CLD
    LDX #$FF
    TXS
    LDA $4015
    STA $0705
    LDA $07F0
    CMP #$A5
    BNE first_boot
    JMP resumed
first_boot:
    LDA #$A5
    STA $07F0
""" + wait_vbl(1) + wait_vbl(2) + """
    LDA #$14
    STA $2000
    LDA #$06
    STA $2001
    LDA #$3F
    STA $2006
    LDA #$20
    STA $2006
    LDA #$07
    STA $2005
    LDA #$5A
    STA $2005
    LDA #$23
    STA $2003
    LDA #$77
    STA $2004
    LDA $2007
    LDA #$3F
    STA $4000
    LDA #$08
    STA $4001
    LDA #$C0
    STA $4002
    STA $4006
    LDA #$0F
    STA $4015
    LDA #$FF
    STA $4003
    STA $4007
    STA $400B
    STA $400F
    LDA #$8F
    STA $4010
    LDA #$20
    STA $4011
    LDA #$10
    STA $4012
    LDA #$01
    STA $4013
    LDA #$1F
    STA $4015
    LDA #$40
    STA $4017
    LDA #$12
    STA $2005
    SED
    SEC
    LDX #$C0
    TXS
    LDA #$11
    LDX #$22
    LDY #$33
armed:
    JMP armed
resumed:
    LDA $2002
    STA $0706
    LDA $2004
    STA $0707
    LDA $2007
    STA $0708
    LDA #$18
    STA $4003
    LDA $4015
    STA $0709
done:
    JMP done
""" + STUBS + vectors())


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "test", "probe")
    os.makedirs(outdir, exist_ok=True)
    for name, entry in sorted(PROBES.items()):
        desc, prog = entry[0], entry[1]
        data = build_ines(prog, entry[2] if len(entry) > 2 else None)
        path = os.path.join(outdir, name + ".nes")
        with open(path, "wb") as f:
            f.write(data)
        print("%-18s %6d bytes  %s" % (name + ".nes", len(data), desc))


if __name__ == "__main__":
    main()
