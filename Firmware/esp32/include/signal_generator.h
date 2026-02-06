#ifndef SIGNAL_GENERATOR_H
#define SIGNAL_GENERATOR_H

#include <Arduino.h>

// ==================== WAVEFORM TYPES ====================
// Order must match the <select id="waveform"> options in data/index.html
enum Waveform : uint8_t {
    WAVE_SINE     = 0,
    WAVE_SQUARE   = 1,
    WAVE_TRIANGLE = 2,
    WAVE_SAWTOOTH = 3,   // ramp up
    WAVE_RAMPDOWN = 4,   // ramp down
    WAVE_NOISE    = 5,   // Galois LFSR white noise
    WAVE_DC       = 6,
    WAVE_COUNT    = 7
};

// ==================== GENERATOR STATE ====================
struct GeneratorState {
    uint8_t  waveform;      // Waveform enum
    uint32_t frequency;     // Hz
    uint8_t  amplitude;     // 0-100 %
    uint8_t  duty;          // 1-99 %, square wave only
    bool     enabled;       // Output on/off

    GeneratorState()
      : waveform(WAVE_SINE), frequency(1000), amplitude(100),
        duty(50), enabled(false) {}
};

extern GeneratorState genState;

// ==================== API ====================
void gen_init();                          // Build LUTs, start I2S, spawn task
void gen_set_waveform(uint8_t w);
void gen_set_frequency(uint32_t hz);
void gen_set_amplitude(uint8_t percent);
void gen_set_duty(uint8_t percent);
void gen_set_enabled(bool on);
const char* gen_waveform_name(uint8_t w);

#endif
