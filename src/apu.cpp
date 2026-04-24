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
    : frame_cycles(0), frame_step(0), frame_irq_inhibit(false),
      frame_mode(false), sample_accum(0.0)
{
    pulse[0] = PulseChannel{};
    pulse[1] = PulseChannel{};
    pulse[1].sweep.is_pulse2 = true;
    tri   = TriangleChannel{};
    noise = NoiseChannel{};
    noise.shift_reg = 1;  // LFSR starts at 1
}

void apu_2a03::write(unsigned short addr, unsigned char val)
{
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
    }
    else if (addr == 0x4017)
    {
        frame_mode = (val >> 7) & 1;
        frame_irq_inhibit = (val >> 6) & 1;
        frame_cycles = 0;
        frame_step = 0;
        if (frame_mode)
            clock_half_frame();  // 5-step mode: immediately clock on write
    }
}

unsigned char apu_2a03::read_status()
{
    unsigned char s = 0;
    if (pulse[0].length > 0) s |= 0x01;
    if (pulse[1].length > 0) s |= 0x02;
    if (tri.length > 0)      s |= 0x04;
    if (noise.length > 0)    s |= 0x08;
    return s;
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

void apu_2a03::tick_frame_sequencer()
{
    // 4-step mode: clocks at ~3728, ~7456, ~11186, ~14914 CPU cycles
    // We approximate each step at 7457 CPU cycles (half-frame = 14914/2)
    // Steps: Q  H  Q  HF   (Q=quarter, H=half, F=irq)
    static const unsigned int STEP_CYCLES_4[4] = { 7457, 7457, 7457, 7457 };
    // 5-step mode: Q H Q - H  (step 3 is skipped)
    static const unsigned int STEP_CYCLES_5[5] = { 7457, 7457, 7457, 7457, 7457 };

    unsigned int step_len = frame_mode ? STEP_CYCLES_5[frame_step % 5]
                                       : STEP_CYCLES_4[frame_step % 4];

    if (frame_cycles >= step_len)
    {
        frame_cycles -= step_len;

        if (!frame_mode) {
            // 4-step: steps 0,2 = quarter; steps 1,3 = half
            if (frame_step == 1 || frame_step == 3)
                clock_half_frame();
            else
                clock_quarter_frame();
            frame_step = (frame_step + 1) & 3;
        } else {
            // 5-step: steps 0,2,4 = quarter; steps 1,3 = half (step 3 skipped for irq)
            if (frame_step == 1 || frame_step == 4)
                clock_half_frame();
            else if (frame_step != 3)
                clock_quarter_frame();
            frame_step = (frame_step + 1) % 5;
        }
    }
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

    // Frame sequencer
    ++frame_cycles;
    tick_frame_sequencer();

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
