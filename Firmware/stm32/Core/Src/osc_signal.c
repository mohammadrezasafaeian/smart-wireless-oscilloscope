#include "osc_signal.h"
#include <string.h>
#include <math.h>

/* ==================== BUFFERS ==================== */
uint16_t adc_buffer[ADC_BUFFER_SIZE] __attribute__((aligned(4)));

/* ==================== EMA FILTER STATE ==================== */
static struct {
    float32_t freq, amp, rms, duty, period;
    uint8_t valid_count;
} meas_filter = {0};

/* ==================== HELPER: EMA UPDATE ==================== */
static inline void ema_update(float32_t *state, float32_t value, uint8_t init) {
    *state = init ? value : (*state + EMA_ALPHA * (value - *state));
}

void reset_measurement_filter(void) {
    memset(&meas_filter, 0, sizeof(meas_filter));
}

/* ==================== DECIMATION ==================== */
void decimate_samples(uint16_t *src, uint16_t src_size,
                      uint16_t *dst, uint16_t dst_size, ScopeMode mode) {
    if(!src_size || !dst_size) return;

    // Source smaller than dest: pad with last sample
    if(src_size <= dst_size) {
        for(uint16_t i = 0; i < dst_size; i++)
            dst[i] = src[(i < src_size) ? i : src_size - 1];
        return;
    }

    for(uint16_t i = 0; i < dst_size; i++) {
        // Map destination indices to source range
        uint32_t start = ((uint32_t)i * (src_size - 1)) / (dst_size - 1);
        uint32_t end = (i == dst_size - 1) ? src_size - 1 :
                       ((uint32_t)(i + 1) * (src_size - 1)) / (dst_size - 1);

        // Clamp bounds
        if(start >= src_size) start = src_size - 1;
        if(end >= src_size) end = src_size - 1;

        uint16_t block_size = end - start + 1;
        uint16_t *block = &src[start];

        switch(mode) {
            case MODE_AVERAGE: {
                uint32_t sum = 0;
                for(uint16_t j = 0; j < block_size; j++) sum += block[j];
                dst[i] = sum / block_size;
                break;
            }
            case MODE_PEAK_DETECT: {
                uint16_t vmin = 4095, vmax = 0;
                for(uint16_t j = 0; j < block_size; j++) {
                    if(block[j] < vmin) vmin = block[j];
                    if(block[j] > vmax) vmax = block[j];
                }
                dst[i] = (i & 1) ? vmax : vmin;  // Alternate min/max
                break;
            }
            default:  // MODE_NORMAL
                dst[i] = block[block_size / 2];
        }
    }
}

/* ==================== TIME DOMAIN MEASUREMENTS ==================== */
void measure_time_domain(uint16_t *buffer, uint32_t size,
                         uint32_t sample_rate, Measurements *m) {
    if(size < 64) { m->valid = 0; return; }

    // Single-pass statistics
    uint32_t sum = 0;
    uint64_t sum_sq = 0;
    uint16_t vmin = 4095, vmax = 0;
    uint32_t high_count = 0;

    for(uint32_t i = 0; i < size; i++) {
        uint16_t val = buffer[i];
        sum += val;
        sum_sq += (uint64_t)val * val;
        if(val < vmin) vmin = val;
        if(val > vmax) vmax = val;
    }

    uint16_t dc = sum / size;
    uint16_t amplitude = vmax - vmin;

    // Convert to millivolts (3.3V reference, 12-bit ADC)
    float32_t raw_amp = (amplitude * 3300.0f) / 4095.0f;
    m->vmax_mv = (vmax * 3300UL) / 4095;
    m->vmin_mv = (vmin * 3300UL) / 4095;

    // RMS from variance
    float32_t variance = (float32_t)(sum_sq / size) - (float32_t)dc * dc;
    float32_t rms_adc;
    arm_sqrt_f32((variance > 0) ? variance : 0, &rms_adc);
    float32_t raw_rms = (rms_adc * 3300.0f) / 4095.0f;

    // Signal too weak check
    if(amplitude < MIN_SIGNAL_AMPLITUDE) {
        m->valid = 0;
        m->frequency_hz = 0;
        m->period_us = 0;
        m->amplitude_mv = (uint16_t)raw_amp;
        m->vrms_mv = (uint16_t)raw_rms;
        m->duty_percent = 50;
        meas_filter.valid_count = 0;
        return;
    }

    // Zero-crossing frequency detection with hysteresis
    uint16_t hyst = (amplitude * ZC_HYSTERESIS_PERCENT) / 100;
    if(hyst < 20) hyst = 20;
    uint16_t thresh_high = dc + hyst, thresh_low = dc - hyst;

    uint32_t rising_edges = 0, first_edge = 0, last_edge = 0;
    uint8_t state = (buffer[0] > dc);

    for(uint32_t i = 1; i < size; i++) {
        if(buffer[i] > dc) high_count++;

        // Schmitt trigger edge detection
        if(!state && buffer[i] > thresh_high) {
            state = 1;
            if(!rising_edges) first_edge = i;
            last_edge = i;
            rising_edges++;
        } else if(state && buffer[i] < thresh_low) {
            state = 0;
        }
    }

    // Calculate frequency from edge timing
    float32_t raw_freq = 0, raw_period = 0;
    if(rising_edges >= 2) {
        uint32_t total_samples = last_edge - first_edge;
        uint32_t periods = rising_edges - 1;
        if(total_samples > 0 && periods > 0) {
            float32_t avg_period = (float32_t)total_samples / periods;
            if(avg_period >= 2.0f) {
                raw_freq = (float32_t)sample_rate / avg_period;
                raw_period = 1000000.0f / raw_freq;
                // Nyquist limit check
                if(raw_freq > sample_rate / 2.0f) raw_freq = raw_period = 0;
            }
        }
    }

    float32_t raw_duty = (float32_t)(high_count * 100) / (size - 1);

    // Invalid frequency check
    if(raw_freq < 0.5f) {
        m->valid = 0;
        m->frequency_hz = 0;
        m->period_us = 0;
        m->amplitude_mv = (uint16_t)raw_amp;
        m->vrms_mv = (uint16_t)raw_rms;
        m->duty_percent = (uint8_t)raw_duty;
        meas_filter.valid_count = 0;
        return;
    }

    // EMA filtering for stable readings
    uint8_t init = (meas_filter.valid_count < 3);
    if(!init && meas_filter.freq > 10.0f) {
        float32_t ratio = raw_freq / meas_filter.freq;
        if(ratio > 1.5f || ratio < 0.67f) init = 1;  // Reset on large jump
    }

    ema_update(&meas_filter.freq, raw_freq, init);
    ema_update(&meas_filter.amp, raw_amp, init);
    ema_update(&meas_filter.rms, raw_rms, init);
    ema_update(&meas_filter.duty, raw_duty, init);
    ema_update(&meas_filter.period, raw_period, init);
    if(meas_filter.valid_count < 255) meas_filter.valid_count++;

    // Output filtered values
    m->frequency_hz = (uint32_t)(meas_filter.freq + 0.5f);
    m->period_us = (uint16_t)(meas_filter.period + 0.5f);
    m->amplitude_mv = (uint16_t)(meas_filter.amp + 0.5f);
    m->vrms_mv = (uint16_t)(meas_filter.rms + 0.5f);
    m->duty_percent = (uint8_t)(meas_filter.duty + 0.5f);
    if(m->duty_percent < 1) m->duty_percent = 1;
    if(m->duty_percent > 99) m->duty_percent = 99;
    m->valid = 1;
}
