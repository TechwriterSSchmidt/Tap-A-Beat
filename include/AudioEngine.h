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
    
    // Play a continuous tone (blocking? NO, needs loop support)
    // For Takt-O-Beat, we will use a simple "fillBuffer" approach called from main loop for Tone.
    void startTone(float frequency);
    void stopTone();
    void updateTone(); // Call in loop when tone is active

    // Set Volume 0-100
    void setVolume(uint8_t volume);
    uint8_t getVolume();

private:
    void generateWave(float frequency, int durationMs, float amplitude);
    
    uint8_t _volume = 50; // 0-100
    
    // Tone generation state
    bool _isTonePlaying = false;
    float _toneFreq = 440.0f;
    float _tonePhase = 0;
    
    // Pre-calculated buffers could go here, 
    // but generating 50ms of audio on the fly on ESP32-S3 is trivial.
};
