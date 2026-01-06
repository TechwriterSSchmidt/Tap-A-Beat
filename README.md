# Takt-O-Beat

> A small, tactilely satisfying, ear-triggering mini metronome for the person I care about.

**Takt-O-Beat** is an ESP32-S3 based digital metronome packed with features in a compact form factor. It is designed to be responsive, precise, and a joy to use.

## Features

- **Precise Timing:** ESP32-S3 based metronome with negligible drift and visual beat indicator.
- **Controlled Audio:** Soft start/stop envelope, limiter with overdrive flag, distinct accent/normal clicks, and digital volume.
- **Smart Inputs:** Tap-tempo via I2S mic, debounced encoder button, and press-turn volume adjustment.
- **Tuner:** Chromatic tuner with A4-adjustable reference and automatic mic gain (AGC).
- **Persistence & Presets:** BPM, time signature, volume, and A4 stored in NVS; three user presets for quick recall/save.
- **Haptics:** PWM haptic pulse on every beat, accent-aware intensity.
- **Display:** 128x128 OLED UI (SH1107/SSD1327) for metronome, tuner, menus, and preset selection.

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

- [ ] Alternate click voices (woodblock/beep/drum).
- [ ] Visual pendulum animation.