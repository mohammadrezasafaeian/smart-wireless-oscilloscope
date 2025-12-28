#ifndef OSC_SIGNAL_H
#define OSC_SIGNAL_H

#include "osc_config.h"
#include "arm_math.h"

/* ==================== EXTERNAL BUFFERS ==================== */
extern uint16_t adc_buffer[ADC_BUFFER_SIZE];

/* ==================== API FUNCTIONS ==================== */

// Downsample src buffer to dst with specified mode
void decimate_samples(uint16_t *src, uint16_t src_size,
                      uint16_t *dst, uint16_t dst_size, ScopeMode mode);

// Time-domain measurements (freq, amplitude, RMS, duty)
void measure_time_domain(uint16_t *buffer, uint32_t size,
                         uint32_t sample_rate, Measurements *m);

// Reset EMA filter state (call on settings change)
void reset_measurement_filter(void);

#endif /* OSC_SIGNAL_H */
