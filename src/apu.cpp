#include "stdafx.h"
#include "apu.h"
#include <cmath>

// ─────────────────────────────────────────
// Triangle sequence
// ─────────────────────────────────────────
const unsigned char TriangleChannel::SEQ[32] = {
    15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
     0, 1, 2, 3, 4, 5,6,7,8,9,10,11,12,13,14,15
};

// ─────────────────────────────────────────
// PulseChannel
// ─────────────────────────────────────────
void PulseChannel::write(unsigned short addr, unsigned char val)
{
    switch (addr & 0x03)
    {
    case 0: // $4000/$4004
        duty = (val >> 6) & 0x03;
        env.loop = (val >> 5) & 1;
        env.constant = (val >> 4) & 1;
        env.volume = val & 0x0F;
        break;
    case 1: // $4001/$4005 – sweep
        sweep.enabled = (val >> 7) & 1;
        sweep.period  = (val >> 4) & 0x07;
        sweep.negate  = (val >> 3) & 1;
        sweep.shift   = val & 0x07;
        sweep.reload  = true;
        break;
    case 2: // $4002/$4006 – timer low
        timer = (timer & 0x0700) | val;
        break;
    case 3: // $4003/$4007 – timer high + length load
        timer = (timer & 0x00FF) | ((val & 0x07) << 8);
        if (enabled) length = LENGTH_TABLE[val >> 3];
        env.reset();
        duty_pos = 0;
        break;
    }
}

void PulseChannel::tick_timer()
{
    if (timer_cur == 0) {
        timer_cur = timer;
        duty_pos = (duty_pos + 1) & 0x07;  // 8 steps
    } else {
        --timer_cur;
    }
}

void PulseChannel::tick_length()
{
    if (!env.loop && length > 0)
        --length;
}

void PulseChannel::tick_sweep()
{
    if (sweep.reload) {
        sweep.divider = sweep.period;
        sweep.reload = false;
    } else if (sweep.divider > 0) {
        --sweep.divider;
    } else {
        sweep.divider = sweep.period;
        if (sweep.enabled && sweep.shift > 0 && !sweep.muting(timer)) {
            timer = (unsigned short)sweep.target(timer);
        }
    }
}

unsigned char PulseChannel::output() const
{
    if (!enabled || length == 0 || sweep.muting(timer))
        return 0;
    return DUTY_TABLE[duty][duty_pos] ? env.output() : 0;
}

// ─────────────────────────────────────────
// TriangleChannel
// ─────────────────────────────────────────
void TriangleChannel::write(unsigned short addr, unsigned char val)
{
    switch (addr & 0x03)
    {
    case 0: // $4008
        control = (val >> 7) & 1;
        linear_reload = val & 0x7F;
        break;
    case 2: // $400A – timer low
        timer = (timer & 0x0700) | val;
        break;
    case 3: // $400B – timer high + length load
        timer = (timer & 0x00FF) | ((val & 0x07) << 8);
        if (enabled) length = LENGTH_TABLE[val >> 3];
        linear_reload_flag = true;
        break;
    }
}

void TriangleChannel::tick_timer()
{
    if (timer_cur == 0) {
        timer_cur = timer;
        if (length > 0 && linear_counter > 0)
            seq_pos = (seq_pos + 1) & 0x1F;
    } else {
        --timer_cur;
    }
}

void TriangleChannel::tick_length()
{
    if (!control && length > 0)
        --length;
}

void TriangleChannel::tick_linear()
{
    if (linear_reload_flag)
        linear_counter = linear_reload;
    else if (linear_counter > 0)
        --linear_counter;
    if (!control)
        linear_reload_flag = false;
}

unsigned char TriangleChannel::output() const
{
    if (!enabled || length == 0 || linear_counter == 0)
        return 0;
    return SEQ[seq_pos];
}

// ─────────────────────────────────────────
// NoiseChannel
// ─────────────────────────────────────────
void NoiseChannel::write(unsigned short addr, unsigned char val)
{
    switch (addr & 0x03)
    {
    case 0: // $400C
        env.loop = (val >> 5) & 1;
        env.constant = (val >> 4) & 1;
        env.volume = val & 0x0F;
        break;
    case 2: // $400E
        mode   = (val >> 7) & 1;
        period = NOISE_PERIOD_TABLE[val & 0x0F];
        break;
    case 3: // $400F
        if (enabled) length = LENGTH_TABLE[val >> 3];
        env.reset();
        break;
    }
}

void NoiseChannel::tick_timer()
{
    if (timer_cur == 0) {
        timer_cur = period;
        // LFSR: feedback from bit0 XOR bit1 (mode=1) or bit0 XOR bit6
        unsigned char feedback_bit = mode ? 6 : 1;
        unsigned short feedback = (shift_reg ^ (shift_reg >> feedback_bit)) & 1;
        shift_reg = (shift_reg >> 1) | (feedback << 14);
    } else {
        --timer_cur;
    }
}

void NoiseChannel::tick_length()
{
    if (!env.loop && length > 0)
        --length;
}

unsigned char NoiseChannel::output() const
{
    if (!enabled || length == 0 || (shift_reg & 1))
        return 0;
    return env.output();
}

// ─────────────────────────────────────────
// apu_2a03
// ─────────────────────────────────────────
apu_2a03::apu_2a03()
    : fhcnt(FRAME_STEP_UNITS), fcnt(0), irq_mode(0), sirq(0),
      dmc_format(0), dmc_dac(0), dmc_addr_latch(0), dmc_size_latch(0), dmc_addr(0), dmc_size(0),
      dmc_shift(0), dmc_bits(0), dmc_buf(0), dmc_have_sample(false), dmc_have_dma(false),
      dmc_acc(1), dmc_period(428), sample_accum(0.0)
{
    memset(reg_shadow, 0, sizeof(reg_shadow));
    pulse[0] = PulseChannel{};
    pulse[1] = PulseChannel{};
    pulse[1].sweep.is_pulse2 = true;
    tri   = TriangleChannel{};
    noise = NoiseChannel{};
    noise.shift_reg = 1;  // LFSR starts at 1
}

void apu_2a03::write(unsigned short addr, unsigned char val)
{
    if (addr >= 0x4000 && addr <= 0x4017) reg_shadow[addr - 0x4000] = val;
    if (addr <= 0x4003)       pulse[0].write(addr, val);
    else if (addr <= 0x4007)  pulse[1].write(addr, val);
    else if (addr <= 0x400B)  tri.write(addr, val);
    else if (addr <= 0x400F)  noise.write(addr, val);
    else if (addr == 0x4015)
    {
        pulse[0].enabled = (val & 0x01) != 0;
        pulse[1].enabled = (val & 0x02) != 0;
        tri.enabled      = (val & 0x04) != 0;
        noise.enabled    = (val & 0x08) != 0;
        if (!pulse[0].enabled) pulse[0].length = 0;
        if (!pulse[1].enabled) pulse[1].length = 0;
        if (!tri.enabled)      tri.length = 0;
        if (!noise.enabled)    noise.length = 0;
        if (val & 0x10) { if (!dmc_size) dmc_prepare(); }
        else dmc_size = 0;
        sirq &= ~0x80;
    }
    else if (addr == 0x4010)
    {
        static const int DMC_PERIOD[16] = { 428, 380, 340, 320, 286, 254, 226, 214,
                                            190, 160, 142, 128, 106, 84, 72, 54 };
        dmc_period = DMC_PERIOD[val & 0xF];
        if (!(val & 0x80)) sirq &= ~0x80;
        dmc_format = val;
    }
    else if (addr == 0x4011) dmc_dac = val & 0x7F;
    else if (addr == 0x4012) dmc_addr_latch = val;
    else if (addr == 0x4013) dmc_size_latch = val;
    else if (addr == 0x4017)
    {
        const unsigned char mode = (val & 0xC0) >> 6;
        fcnt = 0;
        if (mode & 2)
            frame_update();   // 5-step mode clocks immediately (with the previous mode's IRQ rule)
        fcnt = 1;
        fhcnt = FRAME_STEP_UNITS;
        sirq &= ~0x40;
        irq_mode = mode;
    }
}

unsigned char apu_2a03::read_status()
{
    unsigned char s = 0;
    if (pulse[0].length > 0) s |= 0x01;
    if (pulse[1].length > 0) s |= 0x02;
    if (tri.length > 0)      s |= 0x04;
    if (noise.length > 0)    s |= 0x08;
    if (dmc_size > 0)        s |= 0x10;
    s |= sirq;
    sirq &= ~0x40;
    return s;
}

void apu_2a03::dmc_prepare()
{
    dmc_addr = 0x4000 + ((unsigned int)dmc_addr_latch << 6);
    dmc_size = (dmc_size_latch << 4) + 1;
}

void apu_2a03::frame_update()
{
    if (fcnt == 0 && !(irq_mode & 3))
        sirq |= 0x40;
    if (fcnt == 3 && (irq_mode & 2))
        fhcnt += FRAME_STEP_UNITS;
    frame_clock(fcnt);
    fcnt = (fcnt + 1) & 3;
}

void apu_2a03::frame_clock(unsigned char step)
{
    if (!(step & 1)) clock_half_frame();   // length counters and sweeps
    else             clock_quarter_frame();
}

unsigned int apu_2a03::advance(unsigned int cycles, const unsigned char* mem, unsigned char& bus)
{
    unsigned int stall = 0;

    fhcnt -= (int)cycles * 48;
    if (fhcnt <= 0)
    {
        frame_update();
        fhcnt += FRAME_STEP_UNITS;
    }

    // DMC sample fetch: one byte per call, stealing 4 CPU cycles.
    if (dmc_size && !dmc_have_dma)
    {
        stall = 4;
        dmc_buf = mem[0x8000 + dmc_addr];
        bus = dmc_buf;
        dmc_have_dma = true;
        dmc_addr = (dmc_addr + 1) & 0x7FFF;
        if (--dmc_size == 0)
        {
            if (dmc_format & 0x40)      dmc_prepare();
            else if (dmc_format & 0x80) sirq |= 0x80;
        }
    }

    dmc_acc -= (int)cycles;
    while (dmc_acc <= 0)
    {
        if (dmc_have_sample)
        {
            unsigned char old = dmc_dac;
            dmc_dac += ((dmc_shift & 1) << 2) - 2;
            if (dmc_dac & 0x80) dmc_dac = old;
        }
        dmc_acc += dmc_period;
        dmc_bits = (dmc_bits + 1) & 7;
        dmc_shift >>= 1;
        if (dmc_bits == 0)
        {
            if (!dmc_have_dma) dmc_have_sample = false;
            else { dmc_have_sample = true; dmc_shift = dmc_buf; dmc_have_dma = false; }
        }
    }
    return stall;
}

void apu_2a03::clock_quarter_frame()
{
    pulse[0].tick_envelope();
    pulse[1].tick_envelope();
    tri.tick_linear();
    noise.tick_envelope();
}

void apu_2a03::clock_half_frame()
{
    clock_quarter_frame();
    pulse[0].tick_length();
    pulse[1].tick_length();
    tri.tick_length();
    noise.tick_length();
    pulse[0].tick_sweep();
    pulse[1].tick_sweep();
}

short apu_2a03::mix() const
{
    // NES non-linear mixing (approximation)
    unsigned char p1 = pulse[0].output();
    unsigned char p2 = pulse[1].output();
    unsigned char t  = tri.output();
    unsigned char n  = noise.output();

    float pulse_out = (p1 + p2 == 0) ? 0.0f
        : 95.88f / (8128.0f / (p1 + p2) + 100.0f);

    float tnd_out = (t + n == 0) ? 0.0f
        : 159.79f / (1.0f / (t / 8227.0f + n / 12241.0f) + 100.0f);

    float sample = (pulse_out + tnd_out) * 32767.0f;
    if (sample >  32767.0f) sample =  32767.0f;
    if (sample < -32768.0f) sample = -32768.0f;
    return (short)sample;
}

bool apu_2a03::tick(short& sample_out)
{
    // Tick timers every CPU cycle.
    // Pulse timers tick every 2 CPU cycles (APU cycle = 2 CPU cycles).
    // Triangle and noise tick every CPU cycle.
    static unsigned int cpu_cycle = 0;
    ++cpu_cycle;

    // Pulse & noise timer: every 2 CPU cycles
    if (cpu_cycle & 1) {
        pulse[0].tick_timer();
        pulse[1].tick_timer();
        noise.tick_timer();
    }
    // Triangle timer: every CPU cycle
    tri.tick_timer();

    // Sample generation
    sample_accum += 1.0;
    if (sample_accum >= CYCLES_PER_SAMPLE) {
        sample_accum -= CYCLES_PER_SAMPLE;
        sample_out = mix();
        return true;
    }
    return false;
}

void apu_2a03::tick_n(unsigned int cycles, std::vector<short>& buf)
{
    short s;
    for (unsigned int i = 0; i < cycles; ++i)
        if (tick(s))
            buf.push_back(s);
}
