#pragma once

// --- display settings -------------------------------------------------------
// Standard I2C for ESP32 (D32)
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22

// --- visual feedback (WS2812) -----------------------------------------------
#define WS2812_PIN      4   // External NeoPixel
#define WS2812_NUM_LEDS 1

// --- Audio Configuration ----------------------------------------------------
#define SAMPLE_RATE     44100
#define NUM_PRESETS     5
#define AUDIO_TASK_CORE 0
#define AUDIO_TASK_PRIO 2     // Higher than Loop (1)

// --- audio output (I2S Amp) -------------------------------------------------
#define I2S_DOUT      19
#define I2S_BCLK      26
#define I2S_LRC       25

// --- audio input (I2S Microphone such as INMP441) ---------------------------
#define I2S_MIC_SD    35 // Input Only
#define I2S_MIC_WS    23
#define I2S_MIC_SCK   18

// --- haptics (PWM) ---------------------------------------------------------
#define HAPTIC_PIN     13
#define HAPTIC_PWM_FREQ 200
#define HAPTIC_PWM_CH   3

// --- rotary encoder ---------------------------------------------------------
#define ENC_PIN_A     33
#define ENC_PIN_B     32
#define ENC_BUTTON    27

// --- power management -------------------------------------------------------
#define BATTERY_PIN   1  // Analog pin for voltage divider
#define AUTO_OFF_MS   120000 // 2 Minutes
