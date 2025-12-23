# Smart Wireless Oscilloscope

> Dual-MCU oscilloscope with real-time FFT, auto-ranging analog front end, and wireless browser-based display.

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
| Bandwidth | DC — 500 kHz |
| Input Range | ±137 mV to ±26 V (32 auto-selected ranges) |
| Input Impedance | 100 kΩ — 1 MΩ (range-dependent) |
| FFT | 4096-point |
| Connectivity | WiFi AP + WebSocket |
| Display | 128×64 OLED + Browser |

---

## System Architecture

```mermaid
flowchart TB
    subgraph ANALOG["ANALOG FRONT END"]
        PROBE["BNC Input<br>±26V max"] --> AFE["AFE<br>32 Auto-Ranges"]
    end

    subgraph STM32["STM32F4 @ 100 MHz"]
        ADC["ADC 12-bit<br>1 MSPS"] --> DSP["DSP<br>FFT | Measure"]
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

</details>

---

## Features

- **Wireless Display**: Access from any browser — phone, tablet, or PC
- **Auto-Ranging**: 32 gain settings, hardware-safe boot, 10 ms adaptation
- **Acquisition**: Timer-triggered ADC with DMA (Normal, Average, Peak Detect)
- **Analysis**: Real-time 4096-point FFT, auto-measurements (Freq, Vpp, Vrms, Duty)
- **Generator**: PWM output 1 Hz — 100 kHz, variable duty cycle
- **Local Display**: 128×64 OLED for standalone operation

---

## Analog Front End

**Constraints:** Single 3.3V supply | Survives ±26V input | 500 kHz bandwidth (flat across all gains)

### Signal Chain

```mermaid
flowchart LR
    subgraph INPUT["INPUT"]
        BNC(["±26V"])
    end

    subgraph ATTEN["ATTENUATE"]
        DIV["Compensated<br>Divider"]
        RELAY["4 Relay-<br>Switched<br>Ratios"]
    end

    subgraph CONDITION["CONDITION"]
        SHIFT["Level Shift<br>0V → 1.65V"]
        PGA["PGA<br>×1 to ×12"]
    end

    subgraph PROTECT["PROTECT"]
        CLAMP["Clamp +<br>Filter"]
    end

    subgraph MCU["STM32"]
        ADC["ADC"]
        AUTO["Auto-Range"]
    end

    BNC --> DIV
    RELAY -.-> DIV
    DIV --> SHIFT --> PGA --> CLAMP --> ADC --> AUTO
    AUTO -.->|"adjust"| RELAY
    AUTO -.->|"adjust"| PGA

    style ATTEN fill:#fff3cd,stroke:#ffc107
    style CONDITION fill:#d3f9d8,stroke:#40c057
    style PROTECT fill:#ffe8cc,stroke:#fd7e14
    style MCU fill:#e7f5ff,stroke:#339af0
```

**How it works:**
1. **Attenuator** — Relay-switched compensated divider (÷1 to ÷16) handles high voltage
2. **Level shift** — Inverting stage (×−1) translates bipolar signal to 1.65V center
3. **PGA** — Mux-selected feedback sets gain (×1–×12) with bandwidth-matched caps
4. **Protection** — Schottky clamps + RC filter; hardware pull-downs ensure safe boot

MCU monitors ADC codes and steps through 32 combinations (4 divider × 8 PGA) automatically.

### Schematic

<p align="center">
  <img src="Hardware/afe_schematic.svg" alt="AFE Schematic" width="100%">
</p>

---

### Auto-Ranging

| Condition | Action |
|-----------|--------|
| ADC near rails (>97% or <3%) | Reduce gain |
| Signal too small (<25%) | Increase gain |
| Optimal | Hold |

Runs every 10 ms with hysteresis. Boot default: ÷16, ×1 (maximum attenuation).

<details>
<summary>Range Table</summary>

**Attenuator (relay-switched):**

| Relay | R_bot | Ratio | C_top |
|:-----:|:-----:|:-----:|:-----:|
| K0 | 6.81 kΩ | ÷15.7 | 2.2 pF |
| K1 | 21.5 kΩ | ÷5.65 | 5.6 pF |
| K2 | 75 kΩ | ÷2.33 | 15 pF |
| K3 | Bypass | ÷1 | — |

**PGA (mux-switched):**

| Ch | R_f | Gain | C_f | BW |
|:--:|:---:|:----:|:---:|:--:|
| 0 | 10k | ×1 | 33p | 480 kHz |
| 1 | 20k | ×2 | 15p | 530 kHz |
| 2 | 30.1k | ×3 | 10p | 530 kHz |
| 3 | 40.2k | ×4 | 8.2p | 480 kHz |
| 4 | 49.9k | ×5 | 6.8p | 470 kHz |
| 5 | 60.4k | ×6 | 5.6p | 470 kHz |
| 6 | 80.6k | ×8 | 3.9p | 510 kHz |
| 7 | 121k | ×12 | 2.7p | 490 kHz |

**Example combinations:**

| Atten | PGA | Total Gain | Input Range | Resolution |
|:-----:|:---:|:----------:|:-----------:|:----------:|
| ÷15.7 | ×1 | 0.064 | ±25.9 V | 12.7 mV/LSB |
| ÷5.65 | ×2 | 0.35 | ±4.7 V | 2.3 mV/LSB |
| ÷1 | ×12 | 12 | ±137 mV | 67 µV/LSB |

</details>



<details>
<summary>Design Decisions</summary>

| Choice | Rationale |
|--------|-----------|
| Relays for attenuator | Low R_on, no distortion, handles ±26V |
| Compensated divider | Flat response despite capacitive loading |
| Inverting topology | Constant BW and input impedance across gains |
| Matched R_f × C_f | Same ~500 kHz bandwidth at all PGA settings |
| Fixed 22pF C_bot | Absorbs strays into stable NP0 value |

</details>

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
| U3, U4 | Rail-to-rail op-amp | — | Level shift + PGA |
| U5 | CD74HC4051 | — | PGA gain mux |
| U6 | CD74HC238 | — | Relay decoder |
| U7 | ULN2003A | — | Relay driver |
| U8 | AP2112K-3.3 | — | LDO |
| U9 | SSD1306 | 128×64 | OLED |
| K0–K3 | DPDT relay | 5V coil | Attenuator |
| D1, D2 | BAT54S | — | Schottky clamps |
| — | Resistors | 6.81k–121k | Divider + PGA |
| — | Capacitors | 2.2p–33p NP0 | Compensation |

**Estimated BOM: ~$15**

</details>

---

## License

MIT License — See [LICENSE](LICENSE)

---

<p align="center">
  <b>Built by Mohammad Reza Safaeian</b><br><br>
  <a href="mailto:your@email.com">Email</a>
</p>
