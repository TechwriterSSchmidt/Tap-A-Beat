#pragma once

// --- display settings -------------------------------------------------------
// CHECK YOUR DISPLAY CONTROLLER! 
// Common for 128x128 are SH1107 or SSD1327. 
// Standard SSD1306 is usually 128x64.
#define I2C_SDA_PIN 42  // Adjust to your board
#define I2C_SCL_PIN 41  // Adjust to your board

// --- audio output (I2S Amp) -------------------------------------------------
#define I2S_DOUT      16
#define I2S_BCLK      17
#define I2S_LRC       18

// --- audio input (I2S Microphone such as INMP441) ---------------------------
#define I2S_MIC_SD    10
#define I2S_MIC_WS    11 // Also called LRC
#define I2S_MIC_SCK   12 // Also called BCLK

// --- rotary encoder ---------------------------------------------------------
#define ENC_PIN_A     4
#define ENC_PIN_B     5
#define ENC_BUTTON    6

// --- power management -------------------------------------------------------
#define BATTERY_PIN   1  // Analog pin for voltage divider
#define AUTO_OFF_MS   120000 // 2 Minutes
