#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

// Audio Defaults
// SAMPLE_RATE defined in config.h
#define NUM_CHANNELS 2 // Output stereo (duplicated mono) usually works best with generic I2S amps

class AudioEngine {
public:
    AudioEngine();
    void begin();
    
    // Play a sound immediately (signals the audio task)
    // isAccent: true = Higher Pitch (Downbeat/One), false = Lower Pitch
    // isSubdivision: true = Very soft tick for subdivision
    void playClick(bool isAccent, bool isSubdivision = false);
    
    // Play a continuous tone (signals the audio task)
    void startTone(float frequency);
    void stopTone();
    // void updateTone(); // Removed: Handled by internal task

    // Set Volume 0-100
    void setVolume(uint8_t volume);
    uint8_t getVolume();

    // Overdrive flag (set when limiter clamps); returns and clears flag
    bool wasOverdriven();

    // Optional beat callback (for haptics)
    void setBeatCallback(void (*cb)(bool accent)) { _beatCallback = cb; }

private:
    static void taskEntry(void* param);
    void audioLoop();
    TaskHandle_t _audioTaskHandle = NULL;

    volatile uint8_t _volume = 50; // 0-100
    
    // Tone generation state (Shared)
    volatile bool _isTonePlaying = false;
    volatile float _toneFreq = 440.0f;
    
    // Click Trigger (Shared)
    volatile bool _triggerClick = false;
    volatile bool _triggerClickAccent = false;
    volatile bool _triggerClickSub = false;

    // Internal synthesis state (Task only)
    float _tonePhase = 0;
    
    // Reporting
    volatile bool _overdrive = false;

    void (*_beatCallback)(bool accent) = nullptr;
    int16_t applyLimiter(int32_t sample);
};
