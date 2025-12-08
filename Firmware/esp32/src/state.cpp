#include "state.h"

SharedState sharedState = {
    .displayMode = MODE_TIME_DOMAIN,
    .frequency = 1000,
    .timebase = 100,
    .dutyCycle = 50,
    .running = true,
    .lastChangeTime = 0
};