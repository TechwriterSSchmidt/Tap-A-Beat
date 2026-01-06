#include <Arduino.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <Wire.h>
#include "config.h"

// --- Objects ----------------------------------------------------------------
// NOTE: Verify your display controller! 
// U8G2_SH1107_128X128_F_HW_I2C is common for 1.12" OLEDs.
// Replace with U8G2_SSD1327_MIDAS_128X128_F_HW_I2C if using SSD1327.
U8G2_SH1107_128X128_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ I2C_SCL_PIN, /* data=*/ I2C_SDA_PIN);

ESP32Encoder encoder;

// --- Global State -----------------------------------------------------------
int currentBpm = 120;
bool isPlaying = false;
long lastEncoderValue = 0;

void setup() {
  Serial.begin(115200);
  
  // Power up delay
  delay(1000);
  Serial.println("Starting Takt-O-Beat...");

  // Init Encoder
  ESP32Encoder::useInternalWeakPullResistors = UP;
  encoder.attachHalfQuad(ENC_PIN_A, ENC_PIN_B);
  encoder.setCount(currentBpm * 2); // Init with value
  pinMode(ENC_BUTTON, INPUT_PULLUP);

  // Init Display
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);
}

void loop() {
  // Read Encoder
  long newEncoderValue = encoder.getCount() / 2; // Divide by 2 for half-quad
  if (newEncoderValue != lastEncoderValue) {
    if (newEncoderValue < 30) {
        encoder.setCount(30 * 2); 
        newEncoderValue = 30;
    }
    if (newEncoderValue > 300) {
        encoder.setCount(300 * 2);
        newEncoderValue = 300;
    }
    currentBpm = newEncoderValue;
    lastEncoderValue = newEncoderValue;
    Serial.printf("BPM: %d\n", currentBpm);
  }

  // Handle Button
  if (digitalRead(ENC_BUTTON) == LOW) {
      delay(50); // Debounce
      if (digitalRead(ENC_BUTTON) == LOW) {
          isPlaying = !isPlaying;
          while(digitalRead(ENC_BUTTON) == LOW); // Wait release
          Serial.printf("Metronome is now %s\n", isPlaying ? "ON" : "OFF");
      }
  }

  // Draw UI
  u8g2.clearBuffer();
  
  // Title
  u8g2.setFont(u8g2_font_profont12_mf);
  u8g2.drawStr(0, 10, "TAKT-O-BEAT");
  
  // Status Bar
  u8g2.drawLine(0, 12, 128, 12);

  // BPM Big text
  char buf[16];
  sprintf(buf, "%d", currentBpm);
  u8g2.setFont(u8g2_font_logisoso42_tn); // Large font
  int width = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - width) / 2, 70, buf);
  
  // BPM Label
  u8g2.setFont(u8g2_font_profont12_mf);
  u8g2.drawStr(100, 60, "BPM");

  // Play Status
  if (isPlaying) {
      u8g2.drawStr(40, 90, "[ PLAYING ]");
      // Visual ticker animation placeholder
      if ((millis() / (60000 / currentBpm)) % 2 == 0) {
          u8g2.drawDisc(10, 90, 4, U8G2_DRAW_ALL);
          u8g2.drawDisc(118, 90, 4, U8G2_DRAW_ALL);
      }
  } else {
      u8g2.drawStr(45, 90, "[ STOP ]");
  }

  u8g2.sendBuffer();
}
