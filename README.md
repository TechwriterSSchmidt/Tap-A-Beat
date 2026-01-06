# Takt-O-Beat

> A small, tactilely satisfying, ear-triggering mini metronome for the person I care about.

**Takt-O-Beat** is an ESP32-S3 based digital metronome packed with features in a compact form factor. It is designed to be responsive, precise, and a joy to use.

## Features

- **Precise Timing:** Drifts are negligible thanks to the ESP32-S3's dual-core architecture.
- **High-Quality Audio:** High-fidelity metronome clicks via I2S digital audio (MAX98357A Amp).
- **Tactile Control:** An industrial-feel incremental rotary encoder handles BPM adjustments, start/stop, and menu navigation.
- **Smart Inputs:** 
  - **Tap-Tempo:** Tap the case or snap your fingers to set the tempo (via I2S Mic).
  - **Tuner:** Built-in chromatic tuner functionality using real-time FFT analysis.
- **Crisp Display:** 128x128 pixel OLED display for high-contrast visibility in any lighting.
- **Portable:** Powered by a LiPo battery with built-in charging management.

## Hardware Stack

| Component | Model | Purpose |
| :--- | :--- | :--- |
| **MCU** | ESP32-S3 (DevKit-C / Lolin S3) | Brains & Processing |
| **Audio Out** | MAX98357A I2S Amplifier | Driving the speaker |
| **Speaker** | 4Ω / 3W Speaker | Making noise |
| **Audio In** | INMP441 I2S Microphone | Capturing Taps & Tuning |
| **Display** | 1.12" OLED (SH1107/SSD1327) | UI (I2C) |
| **Input** | Rotary Encoder (EC11) | User Interface |
| **Power** | LiPo Battery (e.g., 1000mAh) | Portable power |

## Project Structure

- `src/main.cpp`: Main application logic.
- `include/config.h`: Pin definitions and hardware configuration.
- `platformio.ini`: Dependency management and build environment settings.

## Getting Started

1. **Clone the repo.**
2. **Open in VS Code** with the PlatformIO extension installed.
3. **Check `include/config.h`** to match your specific wiring.
4. **Build and Upload** to your ESP32-S3 board.

## Future Ideas

- [ ] Save settings (last BPM) to non-volatile storage.
- [ ] Different click sounds (Woodblock, Beep, Drum).
- [ ] Visual pendulum animation.