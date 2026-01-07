# Takt-O-Beat

> A small, tactilely satisfying, ear-triggering mini metronome for the person I care about.

**Takt-O-Beat** is an ESP32 based digital metronome packed with features in a compact form factor. It is designed to be responsive, precise, and a joy to use.

## Features

- **Precise Timing:** Drifts are negligible thanks to a dedicated FreeRTOS Audio Task running on the ESP32's Core 0, decoupled from UI logic.
- **High-Quality Audio:** 
  - High-fidelity metronome clicks via I2S digital audio (MAX98357A Amp).
  - Distinct sounds for **Upbeat** (High pitch) and **Downbeat** (Low pitch).
  - Digital Volume control.
- **Tactile Control:** An industrial-feel incremental rotary encoder handles:
  - BPM adjustments.
  - Volume control.
  - Menu navigation.
- **Smart Inputs:** 
  - **Tap-Tempo:** Tap the case or snap your fingers to set the tempo (via I2S Mic).
  - **Tuner (Submenu):** Built-in chromatic tuner functionality using real-time FFT analysis.
- **Advanced Rhythm:** 
  - Support for complex time signatures (e.g., 5/4, 7/8).
  - Visual Beat indicator on display.
- **Crisp Display:** 128x128 pixel OLED display for high-contrast visibility in any lighting.
- **Portable:** Powered by a LiPo battery with built-in charging management.

## Hardware Stack

| Component | Model | Purpose |
| :--- | :--- | :--- |
| **MCU** | [LilyGO TTGO T7 V1.5 Mini32](https://lilygo.cc/products/t7-mini32-v1-5) | Brains & Processing (ESP32-WROVER-B, 8MB PSRAM) |
| **Audio Out** | MAX98357A I2S Amplifier | Driving the speaker |
| **Speaker** | 4Ω / 3W Speaker | Making noise |
| **Audio In** | INMP441 I2S Microphone | Capturing Taps & Tuning |
| **Display** | 1.12" OLED (SH1107/SSD1327) | UI (I2C) |
| **Input** | Rotary Encoder (EC11) | User Interface |
| **Power** | LiPo Battery (e.g., 1000mAh) | Portable power |

**Note on T7 V1.5:** This board uses the ESP32-WROVER-B module. GPIOs 16 and 17 are used internally for PSRAM and are not available. The headers expose GPIO 25 and 27 instead (which we use for I2S Audio Out).

## Project Structure

- `src/main.cpp`: Main application logic.
    - **Core 0:** High-priority Audio Task (Metronome timing, Click generation).
    - **Core 1:** Main Loop (UI, Input handling, Display drawing).
- `include/config.h`: Pin definitions and hardware configuration.
- `platformio.ini`: Dependency management and build environment settings.

## User Interface Walkthrough

Since this project uses a 128x128 OLED, the interface is designed to be high-contrast and readable.

**1. The Metronome Screen (Main View)**
This is the default view. It shows the current BPM, Volume, and provides a visual beat indicator.
```text
+----------------------+
|  [~] [Spkr]          | <-- Vibe/Mute Icons
|                      |
|         120          | <-- BPM (Large Font)
|         BPM          |
|                      |
|      >>  O  <<       | <-- Animated Beat
|      4/4  Vol:10     | <-- Metric & Volume
+----------------------+
```

**2. The Main Menu**
Accessible by clicking the encoder. Navigate by rotating, select by clicking.
```text
+----------------------+
|       - MENU -       |
|                      |
| > Speed: 120 bpm     | <-- Selection
|   Metric: 4/4        |
|   Tap / Thresh       | <-- Tap Settings
|   Tuner              |
|   Volume: 10         |
|   Exit               |
+----------------------+
```

**3. Adjustment Sub-Screens**
For setting precise values like BPM or Time Signature, the UI switches to a focused view.
```text
+----------------------+
|     - SET BPM -      |
|                      |
|                      |
|        120           | <-- Rotate to change
|                      |
|                      |
|    (Click to Set)    |
+----------------------+
```

**4. Tap Tempo (Heart Mode)**
Make the heart beat! The sensitivities threshold is represented by the heart's outline.
Your tapping volume fills the heart. If you fill it completely, a beat is registered.
```text
+----------------------+
|      TAP TEMPO       |
|                      |
|      ,-.  ,-.        |
|     (   \/   )       | <-- Outline (Threshold)
|      \      /        |
|       \ ## /         | <-- Input Level (Fills up)
|        \##/          |
|         \/           |
|                      |
|  Sensitivity: 50%    |
+----------------------+
```

**5. The Tuner**
Uses the INMP441 Microphone to detect pitch.
```text
+----------------------+
|      - TUNER -       |
|                      |
|          A           | <-- Detected Note
|        (440Hz)       |
|                      |
|    ---[  |  ]---     | <-- Tuning Needle
|         OK           |
+----------------------+
```

## Getting Started

1. **Clone the repo.**
2. **Open in VS Code** with the PlatformIO extension installed.
3. **Check `include/config.h`** to match your specific wiring.
4. **Build and Upload** to your ESP32 board.

## Future Ideas

- [ ] Save settings (last BPM) to non-volatile storage.
- [ ] Different click sounds (Woodblock, Beep, Drum).
- [ ] Visual pendulum animation.