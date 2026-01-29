#ifndef OSC_AFE_H
#define OSC_AFE_H

#include "osc_config.h"

/* ==================== AFE CONTROL ====================
 * Front end is a 4-way relay-switched attenuator followed by a CD74HC4051
 * feedback-network PGA: 4 x 8 = 32 discrete full-scale settings.
 *
 * Range select drives a CD74HC238 decoder -> ULN2003 -> relay coils.
 * Gain select drives the 4051 address lines directly.
 */

#define AFE_RANGE_COUNT   4
#define AFE_GAIN_COUNT    8

// Attenuation of each relay range, x1000 to stay in integer maths.
// 15.7:1, 5.65:1, 2.33:1, 1:1  (range 0 is the safe boot default)
extern const uint16_t afe_atten_x1000[AFE_RANGE_COUNT];

// PGA gains provided by the switched feedback network.
extern const uint8_t afe_gain[AFE_GAIN_COUNT];

// Drive the decoder and mux address lines.
void afe_init(void);
void afe_apply(uint8_t range, uint8_t gain_idx);

// The 32 combinations sorted by descending full-scale voltage, packed as
// (range << 4) | gain_index.  Attenuator and PGA steps interleave, so
// sweeping range-then-gain would not be monotonic.
#define AFE_STEP_COUNT  (AFE_RANGE_COUNT * AFE_GAIN_COUNT)
extern const uint8_t afe_order[AFE_STEP_COUNT];

// Full-scale input voltage of one setting, in millivolts.
uint32_t afe_full_scale_mv(uint8_t range, uint8_t gain_idx);

// One-shot autorange.  Sweeps from maximum attenuation upward and stops at
// the last setting that keeps the signal inside the ADC range.  Returns the
// full-scale voltage of the chosen setting, in millivolts.
uint32_t afe_autorange(uint8_t *range_out, uint8_t *gain_out);

#endif /* OSC_AFE_H */
