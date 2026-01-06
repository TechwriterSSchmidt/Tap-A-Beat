# Tap-A-Beat
A professional, tactile, and highly precise digital metronome and multi-tool for musicians.

Tap-A-Beat is an ESP32-S3 based device designed for reliability and responsiveness. It combines a high-precision audio engine with tactile feedback, visual cues, and essential practice tools like a chromatic tuner.

## Features

- **Precise Timing:** Multithreaded audio engine (dedicated core) for drift-free timing, independent of UI updates.
- **Natural Sound:** Synthesized "Woodblock" click sound for a pleasant, organic practicing experience.
- **Visual Feedback:** 
  - **OLED:** Large, easy-to-read beat counter with accent framing.
  - **LED:** WS2812/NeoPixel support (Red=Accent, Blue=Beat).
- **Haptic Feedback:** Integrated PWM haptic engine for silent practice with distinct accent pulses.
- **Smart Inputs:** 
  - **Tap Tempo:** Set BPM by tapping the enclosure (via built-in mic) or clapping.
  - **Encoder:** Debounced rotary control with "Press-and-Turn" volume shortcut.
- **Tuner:** Full chromatic tuner with adjustable A4 reference (400–480Hz) and AGC (Automatic Gain Control) for stable detection.
- **Persistence & Presets:** Automatically saves settings; **5 User Presets** for quick set-list changes.
- **Power Management:** Deep sleep auto-off with wake-on-button.

## Hardware Stack

| Component | GPIO | Purpose |
| :--- | :--- | :--- |
| **MCU** | - | ESP32-S3 (DevKit-C) |
| **Audio Out** | 16/17/18 | MAX98357A I2S Amplifier |
| **Microphone** | 10/11/12 | INMP441 I2S Microphone |
| **Display** | 41/42 | 128x128 OLED (I2C) |
| **Encoder** | 4/5/6 | Rotary Encoder (EC11) |
| **Haptics** | 7 | Vibration Motor (PWM) |
| **LED** | 48 | WS2812 / NeoPixel |
| **Battery** | 1 | Voltage Divider (ADC) |

## Project Structure

- `src/main.cpp`: Main application logic, UI, and state machine.
- `src/AudioEngine.cpp`: High-priority I2S audio task and synthesis.
- `include/config.h`: Central pin mapping and configuration.
- `docs/project-schematics.txt`: Wiring guide.
- `platformio.ini`: Dependency management.

## Getting Started

1. **Clone the repo.**
2. **Open in VS Code** with the PlatformIO extension installed.
3. **Check `include/config.h`** to match your specific wiring.
4. **Build and Upload** to your ESP32-S3 board.

## Operation

- **Metronome Mode**
  - **Start/Stop:** Short press encoder.
  - **Set BPM:** Rotate encoder.
  - **Set Volume:** Press and hold button while rotating.
  - **Tap Tempo:** With playback stopped, tap the case rhythmically.
  - **Menu:** Long press encoder.
- **Tuner Mode**
  - Displays Note, Frequency, and deviation (Cents).
  - Short press enables A4 reference tone (rotate to adjust frequency).
- **Presets**
  - Navigate to **Load Preset** or **Save Preset** in the menu.
  - Select one of 5 slots.
- **Haptics & LED**
  - Haptic feedback on beats.
  - NeoPixel LED indicates beats (Red=Accent, Blue=Weak).
- **Deep Sleep**
  - System enters deep sleep after inactivity. Press button to wake.

## Future Ideas

- [x] Natural Woodblock Sound.
- [x] Visual Beat Counter.
- [x] Multithreading Audio.
- [ ] Poly-rhythm support.
- [ ] Wi-Fi sync (Link).