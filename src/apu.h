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

class apu_2a03 {
public:
    apu_2a03();

    void write(unsigned short addr, unsigned char val);
    unsigned char read_status();

    // Call every CPU cycle. Returns true when a new audio sample is ready.
    // sample_out receives the mixed 16-bit sample when ready.
    bool tick(short& sample_out);

    // Convenience: tick n cycles, append samples to buffer
    void tick_n(unsigned int cycles, std::vector<short>& buf);

private:
    void tick_frame_sequencer();
    void clock_quarter_frame();   // envelope + triangle linear
    void clock_half_frame();      // length + sweep (also calls quarter)
    short mix() const;

    PulseChannel   pulse[2];
    TriangleChannel tri;
    NoiseChannel   noise;

    // Frame sequencer
    unsigned int   frame_cycles;   // CPU cycles since last frame-seq step
    unsigned char  frame_step;     // 0-3 (4-step mode only for now)
    bool           frame_irq_inhibit;
    bool           frame_mode;     // 0=4-step, 1=5-step

    // Sample generation
    // NES CPU runs at ~1,789,773 Hz; target 44100 Hz
    // We accumulate a fractional cycle counter
    double         sample_accum;   // fractional cycles accumulated
    static constexpr double CYCLES_PER_SAMPLE = 1789773.0 / 44100.0;
};
