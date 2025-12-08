
# Smart Wireless Oscilloscope

> Dual-MCU oscilloscope with real-time FFT, auto-ranging input, and wireless browser-based display.

![Status](https://img.shields.io/badge/status-working_prototype-green)
![MCU](https://img.shields.io/badge/MCU-STM32F4%20%2B%20ESP32-blue)
![Sample Rate](https://img.shields.io/badge/sample_rate-1%20MSPS-orange)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

<p align="center">
  <img src="docs/demo.gif" alt="Demo" width="700">
</p>

---

## Specifications

| Parameter | Value |
|-----------|-------|
| Sample Rate | 1 MSPS |
| Resolution | 12-bit |
| Bandwidth | DC — 100 kHz |
| Input Range | ±24V (auto-ranging) |
| Input Impedance | 1 MΩ |
| FFT | 4096-point |
| Connectivity | WiFi AP + WebSocket |
| Display | 128×64 OLED + Browser |

---

## System Architecture

```mermaid
flowchart TB
    subgraph ANALOG["ANALOG"]
        PROBE["Probe<br>±24V"] --> AFE["AFE<br>Auto-Range"]
    end

    subgraph STM32["STM32F4 @ 100 MHz"]
        ADC["ADC 12-bit<br>up to 1 MSPS"] --> DSP["DSP<br>FFT | Measure"]
        DSP --> OLED["OLED"]
    end

    subgraph ESP32["ESP32 @ 240 MHz"]
        SPI_S["SPI"] --> WS["WebSocket"]
    end

    subgraph CLIENT["BROWSER"]
        UI["Phone / Tablet / PC"]
    end

    AFE --> ADC
    DSP ==>|"Waveform<br>SPI @ 6.25 MHz"| SPI_S
    DSP <-->|"Measurements + Commands<br>UART 115200"| WS
    WS <-->|"WiFi AP"| UI

    style ANALOG fill:#fff3cd,stroke:#ffc107
    style STM32 fill:#e7f5ff,stroke:#339af0
    style ESP32 fill:#ffe8cc,stroke:#fd7e14
    style CLIENT fill:#d3f9d8,stroke:#40c057
```

**Dual-MCU rationale:**
- **STM32F4**: Real-time sampling and DSP — timing-critical, cannot tolerate WiFi stack jitter
- **ESP32**: Wireless connectivity — creates WiFi AP, streams data via WebSocket at 20 FPS

No router needed. No software installation. Connect to the oscilloscope's WiFi and open a browser.

<details>
<summary>Power Sequencing</summary>

```mermaid
flowchart LR
    PWR["Power On"] --> STM["STM32<br>boots first<br>~30 ms"]
    STM --> OLED["OLED<br>via I2C<br>~200 ms"]
    PWR --> ESP["ESP32<br>boots parallel<br>~1000 ms"]
    OLED --> READY["System Ready<br>~1.7 s"]
    ESP --> READY

    style STM fill:#e7f5ff,stroke:#339af0
    style OLED fill:#d3f9d8,stroke:#40c057
    style ESP fill:#ffe8cc,stroke:#fd7e14
    style READY fill:#d3f9d8,stroke:#40c057
```

| Event | Time |
|-------|------|
| STM32 GPIO safe | ~30 ms |
| OLED splash | ~200 ms |
| ESP32 WiFi ready | ~1000 ms |
| System ready | ~1.7 s |

**Both MCUs boot in parallel.** STM32 is faster but waits for splash screens. ESP32 is slower but catches up.

</details>

---

## Features

- **Wireless Display**: Access from any browser — phone, tablet, or PC
- **Acquisition**: Timer-triggered ADC with DMA, three modes (Normal, Average, Peak Detect)
- **Analysis**: Real-time FFT with Hanning window, auto-measurements (Frequency, Vpp, Vrms, Duty Cycle)
- **Generator**: PWM output 1 Hz — 100 kHz, variable duty cycle
- **Local Display**: 128×64 OLED for standalone operation

---
## Analog Front End

**Constraints:** Under $10 BOM | Single 3.3V supply | Survives ±24V input

### Signal Chain

```mermaid
flowchart LR
    subgraph INPUT["INPUT"]
        IN(["±24V"])
    end

    subgraph PROTECTION["PROTECT + ATTENUATE"]
        R1["1MΩ"]
        D1["Clamp"]
        MUX["Analog MUX<br>8 gain levels<br>Boot: safest"]
        VGND(["1.65V"])
    end

    subgraph GAIN["AMPLIFY"]
        AMP["x11"]
        D2["Clamp"]
    end

    subgraph MCU["STM32"]
        ADC["Sample"]
        AUTO["Auto-Range"]
    end

    IN --> R1 --> D1 --> MUX
    MUX --> VGND
    MUX --> AMP --> D2 --> ADC --> AUTO
    AUTO -->|"Adjust gain"| MUX

    style D1 fill:#fff3cd,stroke:#ffc107
    style D2 fill:#fff3cd,stroke:#ffc107
    style MUX fill:#e7f5ff,stroke:#339af0
    style AMP fill:#d3f9d8,stroke:#40c057
    style MCU fill:#e7f5ff,stroke:#339af0
```

**How it works:** Resistive voltage divider with 8 selectable ratios. The STM32 samples the signal and adjusts gain automatically — from ±24V down to ±150mV. Boots in safest mode.

### Schematic

<p align="center">
  <img src="Hardware/afe_schematic.svg" alt="AFE Schematic" width="100%">
</p>

---
### Auto-Ranging

| Signal | Action |
|--------|--------|
| Clipping | Lower gain (select smaller resistor) |
| Too weak | Raise gain (select larger resistor or open) |
| Optimal | Hold current range |

Runs every 10ms with hysteresis to prevent rapid switching.

Boot default: Maximum attenuation (CH0) — safe even if ±24V applied before code runs.

<details>
<summary>Gain Ranges</summary>

| Channel | Divider R | Net Gain | Input Range | Notes |
|:-------:|:---------:|:--------:|:-----------:|-------|
| 0 | 10 kΩ | x0.11 | ±24V | Boot default (safe) |
| 1 | 20 kΩ | x0.22 | ±12V | |
| 2 | 50 kΩ | x0.55 | ±5V | |
| 3 | 100 kΩ | x1.1 | ±2.5V | |
| 4 | 200 kΩ | x2.2 | ±1.2V | |
| 5 | 500 kΩ | x5.5 | ±500mV | |
| 6 | 1 MΩ | x11 | ±250mV | |
| 7 | Open | x11 | ±150mV | Direct to amp, max sensitivity |

</details>

<details>
<summary>Protection Philosophy</summary>

**Four-layer defense:**

| Layer | Component | Protects | Against |
|:-----:|-----------|----------|---------|
| 1 | R1 (1MΩ) | All downstream | Overcurrent — limits to 24µA |
| 2 | D1 (BAT54S) | MUX inputs | Overvoltage at input node |
| 3 | Boot Default | ADC | Wrong range — GPIO=000 selects CH0 |
| 4 | D2 (BAT54S) | ADC | Op-amp faults, overshoot, glitches |

ADC is protected even if ±24V applied before MCU boots.

</details>

> **Scalability:** Architecture supports extension to ±220V with input divider, CD4067 (16-ch MUX), and galvanic isolation.

---

## Known Limitations

| Issue | Impact | Status |
|-------|--------|--------|
| No voltage calibration | ±5% accuracy | Future |
| Software trigger only | May miss fast transients | Future |

---

## Hardware

<p align="center">
  <img src="docs/hardware.jpg" alt="Hardware" width="600">
</p>

<details>
<summary>Bill of Materials</summary>

| Ref | Component | Value | Purpose |
|-----|-----------|-------|---------|
| U1 | STM32F411CEU6 | — | Main MCU |
| U2 | ESP32-WROOM-32 | — | WiFi bridge |
| U3 | MCP6002 | — | Op-amp (x11 gain) |
| U4 | CD4051 | — | 8:1 Analog MUX |
| U5 | SSD1306 | 128×64 | OLED display |
| D1, D2 | BAT54S | — | Protection clamps |
| R1 | Resistor | 1MΩ | Input impedance |
| R3, R4 | Resistors | 100k, 10k | Gain setting |
| — | Resistors | 10k—1M | Divider ladder (7 pcs) |

**Estimated BOM: ~$12**

</details>

---

## License

MIT License — See [LICENSE](LICENSE)

---

<p align="center">
  <b>Built by Mohammad Reza Safaeian</b><br><br>
  <a href="mailto:your@email.com">Email</a>
</p>

---

