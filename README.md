# Smart Wireless Oscilloscope

> No software. No cables. Connect to WiFi, open browser, measure signals.

![Status](https://img.shields.io/badge/status-working_prototype-green)
![MCU](https://img.shields.io/badge/MCU-STM32F4%20%2B%20ESP32-blue)
![Sample Rate](https://img.shields.io/badge/sample_rate-1%20MSPS-orange)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

<p align="center">
  <img src="docs/demo.gif" alt="Demo" width="700">
</p>

---

## The Problem

Traditional oscilloscopes require dedicated software, USB cables, driver configuration, and being physically next to the device. Their features are limited by fixed buttons and hardcoded firmware.

---

## The Solution

```mermaid
flowchart LR
    SCOPE["Oscilloscope"] -->|"WiFi"| PHONE["Phone"]
    SCOPE -->|"WiFi"| TABLET["Tablet"]
    SCOPE -->|"WiFi"| PC["PC"]

    style SCOPE fill:#e7f5ff,stroke:#339af0
```

Power on → Connect to WiFi → Open browser → Measure.

**No software. No cables. No drivers.**

---

## Why Web-Based Matters

```mermaid
flowchart LR
    subgraph TRAD["TRADITIONAL SCOPE"]
        B["Fixed Buttons"] --> F["Fixed Features"]
    end

    subgraph THIS["THIS SCOPE"]
        W["Web Interface"] --> U["Unlimited Features"]
    end

    style TRAD fill:#ffe0e0,stroke:#ff6b6b
    style THIS fill:#d4edda,stroke:#28a745
```

The hardware is the platform. The web interface is where intelligence lives.

**Current features:** Auto-ranging with SNR optimization, FFT peak detection, auto-measurements, adaptive streaming, multi-client viewing (8 simultaneous)

**Future additions (no hardware changes):** Protocol decoding, cloud logging, automated testing, remote lab access, AI-assisted anomaly detection

---

## Specifications

| Parameter | Value |
|-----------|-------|
| Sample Rate | 1 MSPS |
| Resolution | 12-bit |
| Bandwidth | DC — 500 kHz (flat across all gains) |
| Input Range | ±137 mV to ±26 V (auto-selected) |
| FFT | 4096-point, top 5 peaks detected |
| Generator | PWM 1 Hz – 100 kHz, 1–99% duty |
| Display | Browser + 128×64 OLED |

---

## System Architecture

```mermaid
flowchart TB
    subgraph ANALOG["ANALOG FRONT END"]
        PROBE["BNC ±26V"] --> ATTEN["Attenuator"]
        ATTEN --> SHIFT["Level Shift"]
        SHIFT --> PGA["PGA"]
        PGA --> PROT["Protection"]
    end

    subgraph STM32["STM32F4 @ 100 MHz"]
        ADC["ADC 1 MSPS"] --> DSP["FFT + Measure"]
        DSP --> OLED["OLED"]
        DSP --> RANGE["Auto-Range"]
    end

    subgraph ESP32["ESP32 @ 240 MHz"]
        WS["WebSocket Server"]
    end

    subgraph CLIENT["BROWSER"]
        UI["Any Device"]
    end

    PROT --> ADC
    RANGE -.->|"Relay Control"| ATTEN
    RANGE -.->|"Mux Control<br>Safe boot: max atten"| PGA
    DSP ==>|"Waveform<br>SPI + DMA"| WS
    DSP <-->|"Measurements<br>UART"| WS
    WS <-->|"WiFi AP"| UI

    style ANALOG fill:#fff3cd,stroke:#ffc107
    style STM32 fill:#e7f5ff,stroke:#339af0
    style ESP32 fill:#ffe8cc,stroke:#fd7e14
    style CLIENT fill:#d3f9d8,stroke:#40c057
```

### Auto-Ranging Strategy

STM32 monitors ADC codes and adjusts gain to **maximize SNR without clipping:**

| Signal Level | Path | Rationale |
|--------------|------|-----------|
| Large (>1.65V) | Attenuator ÷2–÷16, PGA ×1 | Attenuate only, avoid amplifier noise |
| Medium (~1V) | Bypass ÷1, PGA ×1 | Direct path |
| Small (<500mV) | Bypass ÷1, PGA ×2–×12 | Amplify to fill ADC range |

Runs every 10 ms with hysteresis.

**Safe boot:** Hardware pull-downs force maximum attenuation (÷16, ×1) before MCU initializes — ADC protected even if ±26V applied at power-on.

<details>
<summary>Range Examples</summary>

| Input Range | Attenuator | PGA | Total Gain |
|:-----------:|:----------:|:---:|:----------:|
| ±25.9 V | ÷15.7 | ×1 | 0.064 |
| ±9.3 V | ÷5.65 | ×1 | 0.177 |
| ±1.65 V | ÷1 | ×1 | 1 |
| ±412 mV | ÷1 | ×4 | 4 |
| ±137 mV | ÷1 | ×12 | 12 |

PGA gain >×1 only when attenuator bypassed — minimizes noise.

</details>

<details>
<summary>Schematic</summary>

<p align="center">
  <img src="Hardware/afe_preview.png" alt="AFE Schematic" width="100%">
</p>

[Download PDF](Hardware/afe_schematic.pdf)

</details>

---

## Implementation

### STM32F411

| Category | Implementation |
|----------|----------------|
| Peripherals | ADC+DMA, TIM2 (trigger), TIM3 (PWM), SPI+DMA, UART, I2C |
| Architecture | Event-driven superloop with DMA completion flags |
| Acquisition | Timer-triggered ADC, 10 Hz – 1 MSPS dynamic |
| DSP | ARM CMSIS 4096-pt FFT, Hanning window, EMA filtering |
| Measurements | Zero-crossing frequency, Vpp, Vrms, top 5 FFT peaks |
| Decimation | Normal, Average, Peak Detect modes |
| Generator | PWM 1 Hz – 100 kHz, 1–99% duty |

### ESP32

| Category | Implementation |
|----------|----------------|
| Network | WiFi AP (192.168.4.1), AsyncWebSocket, 8 clients |
| Streaming | SPI → Binary WebSocket → Canvas @ 20 FPS |
| Throttling | Adaptive 20/7/1 FPS based on client load |
| Recovery | Browser freeze detection, auto-reconnect |

### Design Decisions

| Decision | Rationale |
|----------|-----------|
| Web-based interface | Unlimited features via software updates |
| WiFi AP mode | Works anywhere, no router needed |
| Dual-MCU | Real-time DSP isolated from WiFi jitter |
| DMA everywhere | Zero CPU overhead for ADC and SPI |
| Hardware safe boot | ADC protected before firmware runs |
| Compensated dividers | Flat frequency response across gains |
| Adaptive streaming | Graceful degradation under load |

---

## Demo

https://github.com/user/repo/raw/main/docs/demo.mp4

---

## Hardware

<p align="center">
  <img src="docs/hardware.jpg" alt="Hardware" width="600">
</p>

<details>
<summary>Bill of Materials ~$15</summary>

| Component | Purpose |
|-----------|---------|
| STM32F411 | MCU: acquisition, DSP, AFE control |
| ESP32 | WiFi AP, WebSocket, web hosting |
| Rail-to-rail op-amps | Level shift + PGA |
| CD74HC4051 | PGA gain mux |
| CD74HC238 + ULN2003A | Relay decoder + driver |
| DPDT relays (×4) | Attenuator |
| SSD1306 OLED | Local display |
| BAT54S | Input protection |

</details>

---

## Known Limitations

| Issue | Status |
|-------|--------|
| No voltage calibration (±5% accuracy) | Future |
| Software trigger only | Future |

---

## License

MIT — See [LICENSE](LICENSE)

---

<p align="center">
  <b>Built by Mohammad Reza Safaeian</b><br>
  <a href="mailto:mohammad.rsafaeian@gmail.com">Email</a>
</p>
