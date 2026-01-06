#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

// Audio Defaults
#define SAMPLE_RATE 44100
#define NUM_CHANNELS 2 // Output stereo (duplicated mono) usually works best with generic I2S amps

class AudioEngine {
public:
    AudioEngine();
    void begin();
    
    // Play a sound immediately (blocking logic minimized)
    // isAccent: true = Higher Pitch (Downbeat/One), false = Lower Pitch
    void playClick(bool isAccent);
    
    // Play a continuous tone (non-blocking, buffer filled from main loop).
    void startTone(float frequency);
    void stopTone();
    void updateTone(); // Call in loop when tone is active

    // Set Volume 0-100
    void setVolume(uint8_t volume);
    uint8_t getVolume();

    // Overdrive flag (set when limiter clamps); returns and clears flag
    bool wasOverdriven();

    // Optional beat callback (for haptics)
    void setBeatCallback(void (*cb)(bool accent)) { _beatCallback = cb; }

private:
    void generateWave(float frequency, int durationMs, float amplitude);
    
    uint8_t _volume = 50; // 0-100
    
    // Tone generation state
    bool _isTonePlaying = false;
    float _toneFreq = 440.0f;
    float _tonePhase = 0;
    bool _overdrive = false;

    void (*_beatCallback)(bool accent) = nullptr;
    int16_t applyLimiter(int32_t sample);
    
    // Pre-calculated buffers could go here, 
    // but generating 50ms of audio on the fly on ESP32-S3 is trivial.
};
