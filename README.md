# Tap-A-Beat
A tactilely satisfying, and highly precise digital metronome and multi-tool for the person I care for.

**Tap-A-Beat** is an ESP32 based digital metronome packed with features in a compact form factor. It is designed to be responsive, precise, and a joy to use.

## Support my projects

If you like this project, consider a tip. Your tip motivates me to continue developing useful stuff for the DIY community. Thank you very much for your support!

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/D1D01HVT9A)


## Table of Contents
- [Features](#features)
- [Functional Overview](#functional-overview)
- [Hardware Stack](#hardware-stack)
- [Project Structure](#project-structure)
- [User Interface Walkthrough](#user-interface-walkthrough)
- [Quick Reference Guide (Simplified)](QUICK_REFERENCE_GUIDE.md)
- [Release Notes (v1.3.0)](RELEASE_NOTES.md)

## Features

| Category | Feature | Description |
| :--- | :--- | :--- |
| **Timing** | **Precise Engine** | Dedicated FreeRTOS Audio Task on Core 0 ensures negligible drift and rock-solid timing. |
| **Audio** | **Natural Sound** | Synthesized "Woodblock" click with distinct Pitch for **Downbeats** (Accents) and **Upbeats**. |
| | **Subdivisions** | Support for **8th**, **16th**, and **Triple** subdivisions. |
| | **Feedback** | Audio Feedback during Tap-Tempo detection. |
| **Controls** | **Smart Inputs** | **Encoder** for everything. Press-and-Turn for Volume. Double-Click for Quick Menu. |
| | **Taptronic** | Tap the case to set BPM. Analyzes accents to detect **Time Signatures** automatically. |
| **Feedback** | **OLED Display** | Clear 128x128 interface with large beats and accent framing. |
| | **LED Ring** | WS2812 Support (Red=Accent, Blue=Beat). |
| | **Vibration** | **Exclusive Haptics:** Separate menu toggle. Motor activates **only at Volume 0** (Silent Practice). |
| **Tools** | **Tempo Trainer** | Automates speed increases over time (Start/End BPM, Step size, Bar interval). |
| | **Practice Timer** | Countdown timer (1-60m) for disciplined sessions. |
| | **Tuner** | Chromatic tuner with A4 reference adjustment (400–480Hz). |
| **System** | **Presets** | Save/Load **50 User Presets** organized in **5 Setlists**. |
| | **Power** | **Auto-Off** after 2 minutes of inactivity. **Wake-on-Button**. |

## Functional Overview & Controls

| Feature | Interaction / Control | Note |
| :--- | :--- | :--- |
| **Metronome** | **Turn Encoder**: BPM.<br>**Click**: Play/Stop.<br>**Push & Turn**: Volume. | High-precision timing (30-300 BPM). |
| **Silent Mode** | **Set Volume to 0**.<br>Vibration is active **if enabled** in Menu. | **Vibration Menu**: Toggle ON/OFF.<br>Prevents Vib+Audio simultaneous use. |
| **Taptronic** | **Menu** -> **Taptronic**.<br>Tap rhythmically. | Now with **Audio Feedback** on detection. |
| **Quick Menu** | **Double Click** Button. | Fast access to Time Sig, Subdivisions, Presets. |
| **Preset Save/Load**| **Menu** -> **Presets**.<br>**Hold Click**: Change Setlist. | Stores BPM, Metric, Volume, Tuner settings. |
| **Tuner** | **Menu** -> **Tuner**.<br>**Click**: Toggle Reference Tone. | Visual flat/sharp indication + Audio Tone. |

## Hardware Stack

| Component | GPIO | Purpose |
| :--- | :--- | :--- |
| **MCU** | [LilyGO TTGO T7 V1.5 Mini32](https://lilygo.cc/products/t7-mini32-v1-5) | Brains & Processing (ESP32-WROVER-B, 8MB PSRAM) |
| **Audio Out** | I2S | MAX98357A I2S Amplifier |
| **Microphone** | I2S | INMP441 I2S Microphone |
| **Display** | I2C | 1.12" OLED (SH1107/SSD1327) |
| **Input** | 32/33 | Rotary Encoder (EC11) |
| **Haptics** | 13 | Vibration Motor (PWM) |
| **LED** | 4 | WS2812 / NeoPixel |
| **Power** | 36 | Battery Voltage Divider |

**Note on T7 V1.5:** This board uses the ESP32-WROVER-B module. GPIOs 16 and 17 are used internally for PSRAM and are not available. The headers expose GPIO 25 and 27 instead (which we use for I2S Audio Out).

## Project Structure

- `src/main.cpp`: Main application logic, UI, and state machine.
- `src/AudioEngine.cpp`: High-priority I2S audio task and synthesis.
- `src/Tuner.cpp`: Microphone handler and FFT logic.
- `include/config.h`: Pin definitions and hardware configuration.
- `platformio.ini`: Dependency management and build environment settings.

## User Interface Walkthrough

since this project uses a 128x128 OLED, the interface is designed to be high-contrast and readable.

**1. Main Controls (Play Screen)**
- **Turn Encoder:** Adjust BPM immediately.
- **Short Click:** Toggle Play / Stop.
- **Push & Turn (Hold and Twist):** Adjust Volume (Volume Overlay appears).
- **Double Click:** Open **Quick Menu** (Time Sig, Subdivisions, Presets).
- **Long Press (> 1s):** Enter **Main Menu** (Trainer, Tuner, Settings).

**2. Feedback & Silent Mode**
Tap-A-Beat supports multi-sensory feedback:
- **Audio:** High-fidelity woodblock sample.
- **Visual (WS2812 LED):** The Top-Mounted LED flashes in sync with the beat.
  - **Red:** Accent (Beat 1).
  - **Blue:** Sub-beats.
- **Haptic (Vibration):** The device vibrates on every beat.
- **Silent Mode:** Turn the Volume down to 0 using **Push & Turn**. The audio will mute, but the **LED** and **Vibration Motor** will continue to operate, allowing for discrete, silent tempo keeping.

**3. The Metronome Screen (Main View)**
This is the default view. It shows the current BPM, Volume, and provides a visual beat indicator.

![Metronome Screen](docs/mockups/screen_metronome.png)

**4. The Quick Menu (Double Click)**
Accessible by double-clicking the encoder. Allows fast changes to Metric, Subdivision, and Presets without stopping the flow.

![Quick Menu](docs/mockups/screen_quick_menu.png)

**5. The Main Menu**
Accessible by holding the encoder. Navigate by rotating, select by clicking.

![Main Menu](docs/mockups/screen_menu.png)

**6. Adjustment Sub-Screens**
For setting precise values like BPM or Time Signature, the UI switches to a focused view.

![Set BPM Screen](docs/mockups/screen_set_bpm.png)

**7. Tap Tempo (Heart Mode)**
Make the heart beat! Emphasize the first beat of a bar to automatically set the **Time Signature** (e.g. 3/4). The UI displays "ACC!" for recognized accents. The sensitivity threshold is represented by the heart's outline.

![Tap Tempo Screen](docs/mockups/screen_tap_tempo.png)
