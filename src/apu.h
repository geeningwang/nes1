#pragma once
#include <vector>

// NES APU - 2A03 sound channels
// Channels: Pulse1, Pulse2, Triangle, Noise (DMC not implemented)
// Output: 44100 Hz mono 16-bit PCM samples

// Length counter lookup table (index = 5-bit value from register)
static const unsigned char LENGTH_TABLE[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

// Pulse duty cycle waveforms (8-step sequences)
static const unsigned char DUTY_TABLE[4][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 },  // 12.5%
    { 0, 1, 1, 0, 0, 0, 0, 0 },  // 25%
    { 0, 1, 1, 1, 1, 0, 0, 0 },  // 50%
    { 1, 0, 0, 1, 1, 1, 1, 1 },  // 75% (negated 25%)
};

// Noise period lookup (NTSC)
static const unsigned short NOISE_PERIOD_TABLE[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

struct Envelope {
    bool start;
    bool loop;        // also halt for length counter
    bool constant;    // constant volume mode
    unsigned char volume;    // constant volume or reload value
    unsigned char divider;
    unsigned char decay;

    void reset() { start = true; }
    void tick() {
        if (start) {
            start = false;
            decay = 15;
            divider = volume;
        } else {
            if (divider == 0) {
                divider = volume;
                if (decay > 0) --decay;
                else if (loop) decay = 15;
            } else {
                --divider;
            }
        }
    }
    unsigned char output() const { return constant ? volume : decay; }
};

struct Sweep {
    bool enabled;
    bool negate;
    bool reload;
    bool is_pulse2;   // pulse 2 uses different negate behavior
    unsigned char period;
    unsigned char shift;
    unsigned char divider;

    int target(unsigned short timer) const {
        int change = timer >> shift;
        if (negate) change = is_pulse2 ? -change : -(change + 1);
        return timer + change;
    }
    bool muting(unsigned short timer) const {
        return timer < 8 || target(timer) > 0x7FF;
    }
};

struct PulseChannel {
    bool enabled;
    unsigned char duty;
    unsigned char duty_pos;
    unsigned short timer;       // 11-bit timer reload
    unsigned short timer_cur;
    unsigned char length;
    Envelope env;
    Sweep sweep;

    void write(unsigned short addr, unsigned char val);
    void tick_timer();
    void tick_length();
    void tick_envelope() { env.tick(); }
    void tick_sweep();
    unsigned char output() const;
};

struct TriangleChannel {
    bool enabled;
    bool control;          // length counter halt / linear counter control
    unsigned char linear_reload;
    unsigned char linear_counter;
    bool linear_reload_flag;
    unsigned short timer;
    unsigned short timer_cur;
    unsigned char length;
    unsigned char seq_pos;

    static const unsigned char SEQ[32];

    void write(unsigned short addr, unsigned char val);
    void tick_timer();
    void tick_length();
    void tick_linear();
    unsigned char output() const;
};

struct NoiseChannel {
    bool enabled;
    bool mode;             // short mode (bit 7 of $400E)
    unsigned short period;
    unsigned short timer_cur;
    unsigned short shift_reg;
    unsigned char length;
    Envelope env;

    void write(unsigned short addr, unsigned char val);
    void tick_timer();
    void tick_length();
    void tick_envelope() { env.tick(); }
    unsigned char output() const;
};

struct NesDumpAccess;

class apu_2a03 {
    friend struct NesDumpAccess;  // state dump (statedump.cpp) reads private state
public:
    apu_2a03();

    void write(unsigned short addr, unsigned char val);
    unsigned char read_status();

    // Advance the CPU-visible APU state (frame counter, length counters, DMC) by the given
    // number of CPU cycles.  Called by the CPU once per instruction, before it executes, with
    // the cycles accumulated since the previous call plus the instruction's base cost
    // (spec O-25).  mem is the CPU address space (for DMC fetches); bus receives the DMA
    // byte.  Returns the CPU cycles stolen by a DMC fetch.
    unsigned int advance(unsigned int cycles, const unsigned char* mem, unsigned char& bus);

    // FCEUSND_Reset(): length counters, channel enables, the frame sequencer, the IRQ flags and the DMC
    // state go back to their reset values; the registers written before are kept (spec O-41).
    void reset();
    void power();                  // reset() plus the registers and the DMC period back to power-on; FCEUX keeps the DMC DMA byte

    // Level of the IRQ line driven by the frame counter and the DMC.
    bool irq_line() const { return (sirq & 0xC0) != 0; }

    // Call every CPU cycle. Returns true when a new audio sample is ready.
    // sample_out receives the mixed 16-bit sample when ready.
    bool tick(short& sample_out);

    // Convenience: tick n cycles, append samples to buffer
    void tick_n(unsigned int cycles, std::vector<short>& buf);

private:
    void frame_update();
    void frame_clock(unsigned char step);
    void dmc_prepare();
    void clock_quarter_frame();   // envelope + triangle linear
    void clock_half_frame();      // length + sweep (also calls quarter)
    short mix() const;

    // Last value written to each of $4000-$4017, for the state dump.
    unsigned char  reg_shadow[0x18];

    PulseChannel   pulse[2];
    TriangleChannel tri;
    NoiseChannel   noise;

    // Frame sequencer: one step every 7457.5 CPU cycles, counted in 1/48 cycle units.
    static constexpr int FRAME_STEP_UNITS = 14915 * 24;
    int            fhcnt;          // time left until the next step
    unsigned char  fcnt;           // next step, 0-3
    unsigned char  irq_mode;       // ($4017 >> 6): bit 0 = IRQ inhibit, bit 1 = 5-step mode
    unsigned char  ch_enable;      // $4015 bits 0-4 as last written, 0 after reset
    unsigned char  sirq;           // bit 6 = frame IRQ flag, bit 7 = DMC IRQ flag

    // DMC
    unsigned char  dmc_format;     // $4010
    unsigned char  dmc_dac;        // output level
    unsigned char  dmc_addr_latch, dmc_size_latch;
    unsigned int   dmc_addr;       // 0..0x7FFF, CPU address is $8000 + dmc_addr
    int            dmc_size;       // bytes left
    unsigned char  dmc_shift, dmc_bits, dmc_buf;
    bool           dmc_have_sample, dmc_have_dma;
    int            dmc_acc, dmc_period;

    // Sample generation
    // NES CPU runs at ~1,789,773 Hz; target 44100 Hz
    // We accumulate a fractional cycle counter
    double         sample_accum;   // fractional cycles accumulated
    static constexpr double CYCLES_PER_SAMPLE = 1789773.0 / 44100.0;
};
