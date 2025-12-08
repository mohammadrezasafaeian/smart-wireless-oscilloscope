#include "structures.h"

// ==================== MEASDATA METHODS ====================
void MeasData::update(uint16_t amp, uint32_t freq, uint16_t period, uint16_t vrms) {
  amp_history[history_idx] = amp;
  freq_history[history_idx] = freq;
  period_history[history_idx] = period;
  vrms_history[history_idx] = vrms;
  
  history_idx = (history_idx + 1) % 8;
  if(history_count < 8) history_count++;
  
  amplitude_mv = 0; frequency_hz = 0; period_us = 0; vrms_mv = 0;
  
  for(uint8_t i = 0; i < history_count; i++) {
    amplitude_mv += amp_history[i];
    frequency_hz += freq_history[i];
    period_us += period_history[i];
    vrms_mv += vrms_history[i];
  }
  
  amplitude_mv /= history_count;
  frequency_hz /= history_count;
  period_us /= history_count;
  vrms_mv /= history_count;
  
  valid = true;
}

void MeasData::reset() {
  history_count = 0;
  history_idx = 0;
  valid = false;
}

// ==================== SIGNALSTATS METHODS ====================
void SignalStats::updateStability(uint32_t freq) {
  if(freq == 0) return;
  
  freq_history[freq_idx] = freq;
  freq_idx = (freq_idx + 1) % 16;
  if(freq_count < 16) freq_count++;
  
  if(freq_count < 4) { stability = 100; return; }
  
  float mean = 0, variance = 0;
  for(uint8_t i = 0; i < freq_count; i++) mean += freq_history[i];
  mean /= freq_count;
  
  for(uint8_t i = 0; i < freq_count; i++) {
    float diff = freq_history[i] - mean;
    variance += diff * diff;
  }
  variance /= freq_count;
  
  float std_dev = sqrtf(variance);
  stability = (mean > 0) ? 100.0f * (1.0f - (std_dev / mean)) : 0;
  if(stability < 0) stability = 0;
  if(stability > 100) stability = 100;
}

void SignalStats::reset() {
  freq_count = 0;
  freq_idx = 0;
  stability = 100;
}