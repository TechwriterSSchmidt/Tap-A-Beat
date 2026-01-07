#pragma once

// --- display settings -------------------------------------------------------
// CHECK YOUR DISPLAY CONTROLLER! 
// Common for 128x128 are SH1107 or SSD1327. 
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// --- audio output (I2S Amp) -------------------------------------------------
#define I2S_DOUT      27
#define I2S_BCLK      26
#define I2S_LRC       25

// --- audio input (I2S Microphone such as INMP441) ---------------------------
#define I2S_MIC_SD    35 // Input-only pin
#define I2S_MIC_WS    32
#define I2S_MIC_SCK   33

// --- rotary encoder ---------------------------------------------------------
#define ENC_PIN_A     19
#define ENC_PIN_B     18
#define ENC_BUTTON    5

// --- power management -------------------------------------------------------
#define BATTERY_PIN   36 // VP (ADC1_CH0)
#define AUTO_OFF_MS   120000 // 2 Minutes
