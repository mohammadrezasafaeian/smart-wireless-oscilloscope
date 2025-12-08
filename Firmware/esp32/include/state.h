#ifndef STATE_H
#define STATE_H

#include <Arduino.h>

// ==================== DISPLAY MODE ====================
enum DisplayMode {
    MODE_TIME_DOMAIN = 0,
    MODE_FREQ_DOMAIN = 1
};

// ==================== SHARED STATE ====================
struct SharedState {
    DisplayMode displayMode;
    uint32_t frequency;
    uint32_t timebase;
    uint8_t dutyCycle;
    bool running;
    uint32_t lastChangeTime;
    
    void reset() {
        displayMode = MODE_TIME_DOMAIN;
        frequency = 1000;
        timebase = 100;
        dutyCycle = 50;
        running = true;
        lastChangeTime = millis();
    }
};

extern SharedState sharedState;

#endif