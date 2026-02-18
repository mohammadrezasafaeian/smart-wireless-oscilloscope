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

    // Generator state, mirrored here so new clients receive it on connect
    uint8_t waveform;       // Waveform enum, see signal_generator.h
    uint8_t amplitude;      // 0-100 %
    bool genEnabled;        // Output on/off

    void reset() {
        displayMode = MODE_TIME_DOMAIN;
        frequency = 1000;
        timebase = 100;
        dutyCycle = 50;
        running = true;
        lastChangeTime = millis();
        waveform = 0;       // sine
        amplitude = 100;
        genEnabled = false;
    }
};

extern SharedState sharedState;

#endif