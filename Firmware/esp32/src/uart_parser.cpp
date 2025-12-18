#include <Arduino.h>
#include "structures.h"
#include "config.h"

extern MeasData meas;
extern HardwareSerial SerialSTM;

// ==================== UART PARSER ====================
String build_measurement_json(MeasData& d) {
  String json = "{\"type\":\"meas\""
                ",\"amp\":" + String((int)d.amplitude_mv) +
                ",\"freq\":" + String((int)d.frequency_hz) +
                ",\"period\":" + String((int)d.period_us) +
                ",\"vrms\":" + String((int)d.vrms_mv) +
                ",\"npeaks\":" + String(d.num_peaks) +
                ",\"pfreqs\":[";

  for (uint8_t i = 0; i < d.num_peaks; i++) {
    json += String(d.peak_freqs[i]);
    if (i < d.num_peaks - 1) json += ",";
  }
  json += "],\"pmags\":[";
  for (uint8_t i = 0; i < d.num_peaks; i++) {
    json += String(d.peak_mags[i]);
    if (i < d.num_peaks - 1) json += ",";
  }

  // FFT config for JS peak positioning
  json += "],\"fftParams\":{";
  json += "\"sampleRate\":" + String(FFT_SAMPLE_RATE);
  json += ",\"fftSize\":" + String(FFT_SIZE);
  json += ",\"displayBins\":" + String(DISPLAY_BINS);
  json += ",\"maxFreq\":" + String((int)MAX_DISPLAY_FREQ);
  json += "}}";

  return json;
}

void parse_measurements(String line) {
  Serial.print("RECV: ");
  Serial.println(line);

  if (!line.startsWith("M:")) return;

  uint16_t amp = 0, period = 0, vrms = 0;
  uint32_t freq = 0;
  uint8_t npeaks = 0;
  uint32_t f[5] = {0};
  uint16_t m[5] = {0};

  int parsed = sscanf(line.c_str(),
                      "M:%hu,%lu,%hu,%hu,%hhu,%lu,%hu,%lu,%hu,%lu,%hu,%lu,%hu,%lu,%hu",
                      &amp, &freq, &period, &vrms, &npeaks,
                      &f[0], &m[0], &f[1], &m[1],
                      &f[2], &m[2], &f[3], &m[3],
                      &f[4], &m[4]);

  if (parsed < 5) return;

  // Update smoothed scalar measurements
  meas.update(amp, freq, period, vrms);

  // Determine how many (freq,mag) pairs were parsed
  uint8_t available_peaks = 0;
  if (parsed >= 7) available_peaks = 1;
  if (parsed >= 9) available_peaks = 2;
  if (parsed >= 11) available_peaks = 3;
  if (parsed >= 13) available_peaks = 4;
  if (parsed >= 15) available_peaks = 5;

  if (npeaks > available_peaks) npeaks = available_peaks;
  if (npeaks > 5) npeaks = 5;
  meas.num_peaks = npeaks;

  // Copy into struct
  for (uint8_t i = 0; i < meas.num_peaks; i++) {
    meas.peak_freqs[i] = f[i];
    meas.peak_mags[i] = m[i];
  }
}

void init_uart() {
  SerialSTM.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("✓ UART initialized");
}

void handle_uart_data(void* ws_handle) {
  static String uartBuffer = "";
  
  while(SerialSTM.available()) {
    char c = SerialSTM.read();
    if(c == '\n') {
      uartBuffer.trim();
      if(uartBuffer.length() > 0) {
        parse_measurements(uartBuffer);
        
        // Send to WebSocket if available
        if(ws_handle && meas.valid) {
          String json = build_measurement_json(meas);
          // Note: Actual WebSocket send would need to be in web_interface.cpp
          // This is a simplification
        }
      }
      uartBuffer = "";
    } else if(c != '\r') {
      uartBuffer += c;
      if(uartBuffer.length() > 200) {
        uartBuffer = "";
      }
    }
  }
}