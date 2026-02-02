#include "osc_afe.h"
#include "main.h"

/* Range select: CD74HC238 A0/A1 -> decoder -> ULN2003 -> relay coils.
 * Gain select: CD74HC4051 A0/A1/A2 on the op-amp feedback network. */
#define RNG_S0_PORT   GPIOA
#define RNG_S0_PIN    GPIO_PIN_4
#define RNG_S1_PORT   GPIOA
#define RNG_S1_PIN    GPIO_PIN_5
#define GAIN_A0_PORT  GPIOB
#define GAIN_A0_PIN   GPIO_PIN_4
#define GAIN_A1_PORT  GPIOB
#define GAIN_A1_PIN   GPIO_PIN_8
#define GAIN_A2_PORT  GPIOB
#define GAIN_A2_PIN   GPIO_PIN_9

/* Relays need time to transfer and stop bouncing before the ADC is believed. */
#define RELAY_SETTLE_MS   8

const uint16_t afe_atten_x1000[AFE_RANGE_COUNT] = { 15700, 5650, 2330, 1000 };
const uint8_t  afe_gain[AFE_GAIN_COUNT] = { 1, 2, 3, 4, 6, 8, 10, 12 };

/* Full scale runs 51.8 V down to 275 mV in 32 steps, none bigger than 2:1.
 * The two mechanisms overlap -- range 1 at x1 (18.6 V) sits between range 0
 * at x2 (25.9 V) and range 0 at x3 (17.3 V) -- so the sweep has to follow
 * this order rather than looping range-then-gain. */
const uint8_t afe_order[AFE_STEP_COUNT] = {
    0x00, 0x01, 0x10, 0x02, 0x03, 0x11, 0x04, 0x20,
    0x05, 0x12, 0x06, 0x13, 0x07, 0x21, 0x30, 0x14,
    0x22, 0x15, 0x23, 0x16, 0x31, 0x17, 0x24, 0x32,
    0x25, 0x33, 0x26, 0x27, 0x34, 0x35, 0x36, 0x37,
};

extern ADC_HandleTypeDef hadc1;

void afe_init(void) {
    // Range 0 is maximum attenuation.  The board also pulls the relay drives
    // low in hardware, so the input divider is safe before this ever runs.
    afe_apply(0, 0);
}

void afe_apply(uint8_t range, uint8_t gain_idx) {
    if(range >= AFE_RANGE_COUNT) range = 0;
    if(gain_idx >= AFE_GAIN_COUNT) gain_idx = 0;

    HAL_GPIO_WritePin(RNG_S0_PORT, RNG_S0_PIN, (range & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RNG_S1_PORT, RNG_S1_PIN, (range & 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GAIN_A0_PORT, GAIN_A0_PIN, (gain_idx & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GAIN_A1_PORT, GAIN_A1_PIN, (gain_idx & 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GAIN_A2_PORT, GAIN_A2_PIN, (gain_idx & 4) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_Delay(RELAY_SETTLE_MS);
}

/* Peak-to-peak of a short blocking capture, in ADC counts. */
static uint16_t probe_amplitude(void) {
    uint16_t vmin = 4095, vmax = 0;

    for(uint16_t i = 0; i < 512; i++) {
        HAL_ADC_Start(&hadc1);
        if(HAL_ADC_PollForConversion(&hadc1, 2) != HAL_OK) continue;
        uint16_t v = HAL_ADC_GetValue(&hadc1);
        if(v < vmin) vmin = v;
        if(v > vmax) vmax = v;
    }
    HAL_ADC_Stop(&hadc1);

    return (vmax > vmin) ? (vmax - vmin) : 0;
}

uint32_t afe_full_scale_mv(uint8_t range, uint8_t gain_idx) {
    if(range >= AFE_RANGE_COUNT) range = 0;
    if(gain_idx >= AFE_GAIN_COUNT) gain_idx = 0;

    // Full scale = 3300 mV x attenuation / gain.
    return (3300UL * afe_atten_x1000[range]) / (1000UL * afe_gain[gain_idx]);
}

uint32_t afe_autorange(uint8_t *range_out, uint8_t *gain_out) {
    uint8_t best = 0;   // packed (range << 4) | gain_idx

    // Walk the ladder from the largest full scale downward.  Because it is
    // sorted, the last setting that still fits is also the one that uses the
    // most of the ADC range.
    for(uint8_t i = 0; i < AFE_STEP_COUNT; i++) {
        uint8_t step = afe_order[i];
        afe_apply(step >> 4, step & 0x0F);

        uint16_t amp = probe_amplitude();

        // Stop before the signal reaches the top of the ADC span: clipped
        // data measures as a square wave no matter what went in.
        if(amp > AFE_TARGET_HIGH) break;

        best = step;

        // Filling enough of the span already; more gain buys little and
        // every extra step is another relay transfer.
        if(amp > AFE_TARGET_LOW) break;
    }

    afe_apply(best >> 4, best & 0x0F);

    if(range_out) *range_out = best >> 4;
    if(gain_out)  *gain_out  = best & 0x0F;

    return afe_full_scale_mv(best >> 4, best & 0x0F);
}
