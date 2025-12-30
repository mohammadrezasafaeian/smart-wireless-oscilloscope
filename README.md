
# Smart Wireless Oscilloscope

> No software. No cables. Connect to WiFi, open browser, measure signals.

![Status](https://img.shields.io/badge/status-working_prototype-green)
![MCU](https://img.shields.io/badge/MCU-STM32F4%20%2B%20ESP32-blue)
![Sample Rate](https://img.shields.io/badge/sample_rate-1%20MSPS-orange)
![FFT](https://img.shields.io/badge/FFT-4096--point-purple)
![Generator](https://img.shields.io/badge/Signal_Gen-1Hz--100kHz-brightgreen)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

<p align="center">
  <img src="docs/demo-frequency-domain.gif" alt="Frequency Domain Demo" width="700">
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

## Key Features

<table>
<tr>
<td align="center" width="25%">
<h3>Wireless First</h3>
WiFi AP mode<br>
No cables, no drivers<br>
Works with any device
</td>
<td align="center" width="25%">
<h3>Spectrum Analyzer</h3>
4096-point FFT<br>
Hanning window<br>
Top 5 peak detection
</td>
<td align="center" width="25%">
<h3>Signal Generator</h3>
Built-in PWM output<br>
1 Hz – 100 kHz<br>
Self-test capability
</td>
</tr>
</table>

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

**Current features:** Auto-ranging with SNR optimization, FFT spectrum analysis with peak detection, auto-measurements, adaptive streaming, multi-client viewing, internal signal generator for self-test

**Future additions (no hardware changes):** Protocol decoding, cloud logging, automated testing, remote lab access, AI-assisted anomaly detection

---

## Demo

### Time Domain Analysis

Waveform capture and auto-measurements using the internal signal generator:

<p align="center">
  <img src="docs/demo-time-domain.gif" alt="Time Domain Demo" width="650">
</p>

### Frequency Domain Analysis

FFT spectrum with peak detection using the internal signal generator:

<p align="center">
  <img src="docs/demo-frequency-domain.gif" alt="Frequency Domain Demo" width="650">
</p>

### External Signal Test

Measuring audio signal from a phone:

<p align="center">
  <a href="https://github.com/mohammadrezasafaeian/smart-wireless-oscilloscope/raw/main/docs/demo-video.mp4">
    <img src="docs/video-thumbnail.png" alt="Watch demo with audio" width="650">
  </a>
  <br>
  <em>🔊 Click to watch with audio</em>
</p>>

---

## Specifications

| Parameter | Value |
|-----------|-------|
| Sample Rate | 1 MSPS |
| Resolution | 12-bit |
| Bandwidth | DC — 500 kHz |
| Input Range | ±137 mV to ±26 V (auto) |
| FFT | 4096-point, top 5 peaks |
| Generator | PWM 1 Hz – 100 kHz |
| Display | Browser + OLED |

---

## System Architecture

```mermaid
flowchart TB
    subgraph ANALOG["ANALOG FRONT END"]
        PROBE["BNC ±26V"] --> AFE["Auto-Range AFE<br>32 gain settings<br>Safe boot: max atten"]
    end

    subgraph STM32["STM32F4 @ 100 MHz"]
        ADC["ADC 1 MSPS"] --> DSP["FFT + Measure"]
        DSP --> OLED["OLED"]
    end

    subgraph ESP32["ESP32 @ 240 MHz"]
        WS["WebSocket Server"]
    end

    subgraph CLIENT["BROWSER"]
        UI["Any Device"]
    end

    AFE --> ADC
    DSP -.->|"Gain Control"| AFE
    DSP ==>|"SPI + DMA"| WS
    DSP <-->|"UART"| WS
    WS <-->|"WiFi AP"| UI

    style ANALOG fill:#fff3cd,stroke:#ffc107
    style STM32 fill:#e7f5ff,stroke:#339af0
    style ESP32 fill:#ffe8cc,stroke:#fd7e14
    style CLIENT fill:#d3f9d8,stroke:#40c057
```

| Component | Role |
|-----------|------|
| STM32F411 | Sampling, DSP, measurements, AFE control |
| ESP32 | WiFi AP, WebSocket streaming, web UI |
| Dual-MCU | Isolates real-time DSP from WiFi jitter |

---


## Analog Front End

**Goals:** Single 3.3V supply | Survives ±26V | 500 kHz bandwidth

```mermaid
flowchart LR
    BNC["±26V"] --> ATTEN["Attenuator<br>÷1 to ÷16<br>Relay-switched"]
    ATTEN --> SHIFT["Level Shift<br>0V → 1.65V"]
    SHIFT --> PGA["PGA<br>×1 to ×12<br>Mux-switched"]
    PGA --> PROT["Protection<br>Clamp + Filter"]
    PROT --> ADC["ADC"]

    style ATTEN fill:#fff3cd,stroke:#ffc107
    style PGA fill:#d3f9d8,stroke:#40c057
    style PROT fill:#ffe8cc,stroke:#fd7e14
```

| Stage | Function |
|-------|----------|
| Attenuator | 4 relay-switched compensated dividers |
| Level Shift | Translates bipolar to 1.65V center |
| PGA | 8 mux-switched gains, bandwidth-matched |
| Protection | Schottky clamps, RC filter |

### Schematic

<p align="center">
  <a href="Hardware/afe_schematic.png">
    <img src="Hardware/afe_schematic.png" alt="AFE Schematic" width="100%">
  </a>
  <br>
  <em>Click to view full resolution</em>
</p>

### Auto-Ranging

STM32 adjusts gain to **maximize SNR without clipping:**

| Signal | Strategy |
|--------|----------|
| Large (>1.65V) | Attenuate only, PGA ×1 |
| Medium | Direct path |
| Small (<500mV) | Amplify with PGA |

**Safe boot:** Hardware pull-downs force maximum attenuation before MCU initializes — ADC protected even if ±26V applied at power-on.

<details>
<summary>Range Examples</summary>

| Input Range | Attenuator | PGA | Total Gain |
|:-----------:|:----------:|:---:|:----------:|
| ±25.9 V | ÷15.7 | ×1 | 0.064 |
| ±9.3 V | ÷5.65 | ×1 | 0.177 |
| ±1.65 V | ÷1 | ×1 | 1 |
| ±412 mV | ÷1 | ×4 | 4 |
| ±137 mV | ÷1 | ×12 | 12 |

</details>


---

## Implementation

### STM32F411

| Category | Implementation |
|----------|----------------|
| Acquisition | Timer-triggered ADC + DMA, 10 Hz – 1 MSPS |
| DSP | ARM CMSIS 4096-pt FFT, Hanning window |
| Measurements | Frequency, Vpp, Vrms, top 5 FFT peaks |
| Decimation | Normal, Average, Peak Detect modes |
| Generator | PWM 1 Hz – 100 kHz, 1–99% duty |

### ESP32

| Category | Implementation |
|----------|----------------|
| Network | WiFi AP (192.168.4.1), WebSocket, 8 clients |
| Streaming | Binary WebSocket → Canvas @ 20 FPS |
| Resilience | Adaptive throttling, auto-reconnect |

### Design Decisions

| Decision | Rationale |
|----------|-----------|
| Web-based | Unlimited features, no install |
| WiFi AP | Works anywhere, no router |
| Dual-MCU | DSP isolated from WiFi jitter |
| DMA | Zero CPU overhead |
| Hardware safe boot | ADC protected before firmware runs |
| Internal generator | Self-test capability, no external equipment needed |

---

## Hardware

<p align="center">
  <img src="docs/hardware.jpg" alt="Hardware" width="600">
</p>

<details>
<summary>Bill of Materials ~$15</summary>

| Component | Purpose |
|-----------|---------|
| STM32F411 | MCU: acquisition, DSP, control |
| ESP32 | WiFi, WebSocket, web UI |
| Op-amps + CD74HC4051 | AFE signal conditioning |
| CD74HC238 + ULN2003A | Relay control |
| DPDT relays (×4) | Attenuator |
| SSD1306 OLED | Local display |
| BAT54S | Input protection |

</details>

---

## Known Limitations

| Issue | Status |
|-------|--------|
| No voltage calibration (±5%) | Future |
| Software trigger only | Future |

---

## License

MIT — See [LICENSE](LICENSE)

---

<p align="center">
  <b>Built by Mohammad Reza Safaeian</b><br>
  <a href="mailto:mohammad.rsafaeian@gmail.com">Email</a>
</p>
