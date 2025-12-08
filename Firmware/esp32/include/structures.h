#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <Arduino.h>

// ==================== MEASUREMENT DATA STRUCTURE ====================
struct MeasData {
  float amplitude_mv;
  float frequency_hz;
  float period_us;
  float vrms_mv;
  uint32_t peak_freqs[5];
  uint16_t peak_mags[5];
  uint8_t num_peaks;
  bool valid;
  
  float amp_history[8], freq_history[8], period_history[8], vrms_history[8];
  uint8_t history_idx;
  uint8_t history_count;
  
  void update(uint16_t amp, uint32_t freq, uint16_t period, uint16_t vrms);
  void reset();
};

// ==================== SIGNAL STATISTICS STRUCTURE ====================
struct SignalStats {
  float stability;
  uint32_t freq_history[16];
  uint8_t freq_idx;
  uint8_t freq_count;
  
  void updateStability(uint32_t freq);
  void reset();
};

// ==================== SCOPE STATE (Synced Across All Clients) ====================
// This tracks the current UI state so new clients get the right initial state
// and all clients stay in sync
struct ScopeState {
  // Settings that go to STM32
  uint8_t displayMode;      // 0=Time, 1=FFT (X: command)
  uint16_t timebase;        // µs/div (T: command)
  uint32_t genFrequency;    // Generator Hz (F: command)
  uint8_t duty;             // Duty % (D: command)
  uint8_t acqMode;          // 0=Normal, 1=Avg, 2=Peak (M: command)
  
  // Frontend-only settings (no STM32 command)
  uint16_t voltageScale;    // mV/div (V: - display only)
  bool measEnabled;         // Measurements overlay (E:)
  
  ScopeState() : 
    displayMode(0), timebase(100), genFrequency(1000), 
    duty(50), acqMode(0), voltageScale(500), measEnabled(false) {}
};

#endif