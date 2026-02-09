// ==================== DDS SIGNAL GENERATOR ====================
// Phase-accumulator DDS feeding the ESP32 internal DAC over I2S.
//
// A 32-bit phase accumulator is advanced by a tuning word each sample.  The
// top LUT_BITS of the accumulator index a 1024-entry lookup table, so output
// frequency is set purely by arithmetic and never by re-timing the hardware:
//
//     tuning_word = f_out * 2^32 / f_sample
//
// The I2S peripheral clocks samples out of a DMA ring at a fixed rate, so the
// generator costs almost no CPU and is immune to WiFi task jitter.  The task
// is pinned to core 1; the network stack lives on core 0.

#include "signal_generator.h"
#include "config.h"
#include <math.h>

GeneratorState genState;

// ==================== DDS CONSTANTS ====================
static constexpr uint32_t LUT_BITS   = 10;
static constexpr uint32_t LUT_SIZE   = 1 << LUT_BITS;         // 1024 entries
static constexpr uint32_t LUT_SHIFT  = 32 - LUT_BITS;         // phase -> index
static constexpr uint32_t SAMPLE_RATE = 200000;               // 200 kSPS
static constexpr uint32_t DMA_BUF_LEN = 256;
static constexpr uint32_t DMA_BUF_CNT = 4;

// DAC is 8-bit on the ESP32, presented in the high byte of a 16-bit word.
static constexpr uint16_t DAC_MID = 128;

// ==================== LOOKUP TABLES ====================
// Built once at boot; amplitude is applied per sample so a volume change
// never needs the tables rebuilt.
static uint8_t lut_sine[LUT_SIZE];
static uint8_t lut_triangle[LUT_SIZE];
static uint8_t lut_sawtooth[LUT_SIZE];

static volatile uint32_t phase_acc   = 0;
static volatile uint32_t tuning_word = 0;


// ==================== TABLE CONSTRUCTION ====================
static void build_tables() {
    for (uint32_t i = 0; i < LUT_SIZE; i++) {
        float ph = (2.0f * (float)M_PI * i) / LUT_SIZE;
        lut_sine[i]     = (uint8_t)(127.5f + 127.0f * sinf(ph));
        lut_sawtooth[i] = (uint8_t)((i * 255) / (LUT_SIZE - 1));
        // Triangle: rise over the first half, fall over the second.
        lut_triangle[i] = (i < LUT_SIZE / 2)
                        ? (uint8_t)((i * 255) / (LUT_SIZE / 2 - 1))
                        : (uint8_t)(((LUT_SIZE - 1 - i) * 255) / (LUT_SIZE / 2));
    }
}

// Recompute the tuning word.  Uses a 64-bit intermediate: f * 2^32 overflows
// 32 bits for anything above ~1 Hz.
static void update_tuning_word() {
    uint64_t tw = ((uint64_t)genState.frequency << 32) / SAMPLE_RATE;
    tuning_word = (uint32_t)tw;
}

// ==================== SAMPLE GENERATION ====================
// Returns one 8-bit DAC code for the current phase.
static inline uint8_t next_sample() {
    phase_acc += tuning_word;
    uint32_t idx = phase_acc >> LUT_SHIFT;      // top bits index the LUT
    uint8_t raw;

    switch (genState.waveform) {
        case WAVE_SINE:
            raw = lut_sine[idx];
            break;

        case WAVE_TRIANGLE:
            raw = lut_triangle[idx];
            break;

        case WAVE_SAWTOOTH:
            raw = lut_sawtooth[idx];
            break;

        case WAVE_RAMPDOWN:
            raw = 255 - lut_sawtooth[idx];
            break;

        case WAVE_DC:
        default:
            raw = 255;
            break;
    }

    if (!genState.enabled) return DAC_MID;
    return raw;
}

// ==================== PUBLIC API ====================
void gen_init() {
    build_tables();
    update_tuning_word();

    Serial.println("DDS tables built");
}

void gen_set_waveform(uint8_t w) {
    if (w < WAVE_COUNT) genState.waveform = w;
}

void gen_set_frequency(uint32_t hz) {
    if (hz < 1) hz = 1;
    if (hz > SAMPLE_RATE / 2) hz = SAMPLE_RATE / 2;   // Nyquist
    genState.frequency = hz;
    update_tuning_word();
}

void gen_set_amplitude(uint8_t percent) {
    genState.amplitude = (percent > 100) ? 100 : percent;
}

void gen_set_duty(uint8_t percent) {
    if (percent < 1)  percent = 1;
    if (percent > 99) percent = 99;
    genState.duty = percent;
}

void gen_set_enabled(bool on) {
    genState.enabled = on;
    if (!on) phase_acc = 0;    // restart cleanly on the next enable
}

const char* gen_waveform_name(uint8_t w) {
    switch (w) {
        case WAVE_SINE:     return "Sine";
        case WAVE_SQUARE:   return "Square";
        case WAVE_TRIANGLE: return "Triangle";
        case WAVE_SAWTOOTH: return "Sawtooth";
        case WAVE_RAMPDOWN: return "Ramp Down";
        case WAVE_NOISE:    return "Noise";
        case WAVE_DC:       return "DC";
        default:            return "Unknown";
    }
}
