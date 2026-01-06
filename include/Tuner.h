#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include "arduinoFFT.h"
#include "config.h"

#define MIC_SAMPLE_RATE 16000
#define FFT_SAMPLES 1024 // Power of 2
#define NOISE_THRESHOLD 1000 // Adjust based on mic sensitivity

class Tuner {
public:
    Tuner();
    void begin();
    void stop(); // Stop I2S to save power/conflict
    
    // Returns frequency in Hz, or 0 if silent/noise
    float getFrequency();
    
    // Helper to get Note name and Cents deviation
    // returns string like "A4", fills cents (-50 to +50)
    String getNote(float frequency, int &cents);

private:
    arduinoFFT FFT;
    
    // I2S Read Buffers
    int32_t i2s_raw_buffer[FFT_SAMPLES]; // INMP441 is 24-bit (32bit container)
    double vReal[FFT_SAMPLES];
    double vImag[FFT_SAMPLES];
    
    bool _initialized = false;
};
