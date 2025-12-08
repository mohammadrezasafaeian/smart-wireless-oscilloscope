#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================== NETWORK CONFIGURATION ====================
extern const char* SSID;
extern const char* PASSWORD;

// ==================== BUFFER CONFIGURATION ====================
static constexpr uint32_t BUFFER_SIZE = 512;

// ==================== WEBSOCKET CONFIGURATION ====================
static constexpr uint32_t MAX_WS_CLIENTS = 4;
static constexpr uint32_t WS_TIMEOUT_MS = 15000;      // Increased timeout
static constexpr uint32_t WS_PING_INTERVAL = 5000;
static constexpr uint32_t WS_BINARY_INTERVAL = 80;    // Slower: ~12 FPS (was 50ms/20fps)
static constexpr uint32_t WS_TEXT_INTERVAL = 250;     // Slower: 4 Hz (was 100ms/10hz)

// ==================== FFT CONFIGURATION ====================
static constexpr uint32_t FFT_SIZE = 4096;
static constexpr uint32_t FFT_SAMPLE_RATE = 500000;
static constexpr float HZ_PER_BIN = (float)FFT_SAMPLE_RATE / FFT_SIZE;
static constexpr uint32_t DISPLAY_BINS = 256;
static constexpr float MAX_DISPLAY_FREQ = (FFT_SIZE / 2) * HZ_PER_BIN;

// ==================== PIN CONFIGURATION ====================
#define SPI_MOSI_PIN 23
#define SPI_MISO_PIN 19
#define SPI_SCLK_PIN 18
#define SPI_CS_PIN 5

#define UART_RX_PIN 16
#define UART_TX_PIN 17
#define UART_BAUD 115200

#endif