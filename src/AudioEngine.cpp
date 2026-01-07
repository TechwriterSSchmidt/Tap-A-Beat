#include "AudioEngine.h"

AudioEngine::AudioEngine() {
}

void AudioEngine::begin() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);

    // Launch Audio Task
    xTaskCreatePinnedToCore(
        AudioEngine::taskEntry,
        "AudioTask",
        4096,
        this,
        AUDIO_TASK_PRIO,
        &_audioTaskHandle,
        AUDIO_TASK_CORE
    );
}

void AudioEngine::taskEntry(void* param) {
    AudioEngine* instance = static_cast<AudioEngine*>(param);
    instance->audioLoop();
    vTaskDelete(NULL);
}

void AudioEngine::setVolume(uint8_t volume) {
    if (volume > 100) volume = 100;
    _volume = volume;
}

uint8_t AudioEngine::getVolume() {
    return _volume;
}

bool AudioEngine::wasOverdriven() {
    bool o = _overdrive;
    if (o) _overdrive = false; // Reset locally
    return o;
}

void AudioEngine::stopTone() {
    _isTonePlaying = false;
}

void AudioEngine::startTone(float frequency) {
    _toneFreq = frequency;
    _isTonePlaying = true;
}

void AudioEngine::playClick(bool isAccent, bool isSubdivision) {
    // Signal the task
    _triggerClickAccent = isAccent;
    _triggerClickSub = isSubdivision;
    _triggerClick = true;
    
    // Fire callback for haptics/LED immediately (UI thread)
    // Only fire main beat callback for normal beats, or maybe implement a separate one?
    // Current design: Haptics fire on every 'playClick'.
    // We update callback to only fire HARD on beats, SOFT on subs?
    // User requested features 1, 2, 3, 5. 2 is Subdivisions.
    if (_beatCallback) {
        // If it's a subdivision, we might want weaker feedback?
        // For now, pass 'accent' state as false for Subdivisions, but maybe we need a param.
        // Actually, main.cpp handles haptics based on 'accent' param.
        // If we pass false, it gives a normal beat pulse.
        // Subdivision should probably be NO haptics or very weak?
        // Let's rely on main.cpp logic.
        // If it is subdivision -> treat as non-accent? Or handle logic there?
        // Let's just modify the callback signature? No, that breaks main.cpp comp.
        // We will just call it. But wait, main.cpp treats !accent as 'Blue' LED.
        // We might want a different LED for Sub?
        // For now, let's treat Subdivision as non-accent.
        _beatCallback(isAccent);
    }
}

int16_t AudioEngine::applyLimiter(int32_t sample) {
    const int32_t softLimit = 28000;
    if (sample > softLimit) {
        _overdrive = true;
        sample = softLimit + (sample - softLimit) / 4; 
    } else if (sample < -softLimit) {
        _overdrive = true;
        sample = -softLimit + (sample + softLimit) / 4;
    }

    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    return (int16_t)sample;
}

void AudioEngine::audioLoop() {
    const size_t chunkSamples = 128; // Low latency chunks (approx 3ms)
    int16_t buffer[chunkSamples * 2]; // Stereo interleaved
    size_t bytes_written;

    // Synthesis State for Click
    float clickEnv = 0.0f;
    float clickPhase = 0.0f;
    float clickInc = 0.0f;
    float clickDecay = 0.999f;
    
    // Synthesis State for Tone
    float tonePhase = 0.0f;

    while (true) {
        // --- Event Handling ---
        if (_triggerClick) {
            _triggerClick = false;
            // "Woodblock" Synthesis
            // High frequency sine with exponential decay
            float freq;
            if (_triggerClickSub) {
                freq = 2000.0f; // Higher/Thinner for sub
                clickDecay = 0.995f; // Faster decay (shorter tick)
            } else {
                freq = _triggerClickAccent ? 2500.0f : 1600.0f;
                // Fast decay for percussive sound
                // 0.9985 ^ 2000 samples (~45ms) -> ~0.05 amplitude
                clickDecay = 0.9985f; 
            }
            clickInc = (2.0f * PI * freq) / SAMPLE_RATE;
            
            clickPhase = 0.0f;
            clickEnv = _triggerClickSub ? 0.4f : 1.0f; // Soft volume for sub
        }

        // --- Synthesis ---
        float vol = (float)_volume / 100.0f;
        float toneInc = (2.0f * PI * _toneFreq) / SAMPLE_RATE;
        bool toneOn = _isTonePlaying;

        for (int i = 0; i < chunkSamples; i++) {
            float mix = 0.0f;

            // 1. Click Synthesis
            if (clickEnv > 0.0001f) {
                // Initial burst of noise for "attack"? 
                // Simple sine burst is usually clean enough for metronome.
                mix += sin(clickPhase) * clickEnv;
                clickPhase += clickInc;
                if (clickPhase > 2.0f * PI) clickPhase -= 2.0f * PI;
                clickEnv *= clickDecay;
            }

            // 2. Tone Synthesis
            if (toneOn) {
                mix += sin(tonePhase) * 0.7f; // continuous tone lower gain
                tonePhase += toneInc;
                if (tonePhase > 2.0f * PI) tonePhase -= 2.0f * PI;
            }

            // 3. Master Volume & Limiter
            // Scale to int16 range (approx 30000 to leave headroom)
            int32_t s = (int32_t)(mix * 30000.0f * vol);
            int16_t finalSample = applyLimiter(s);

            // Stereo Copy
            buffer[i * 2] = finalSample;
            buffer[i * 2 + 1] = finalSample;
        }

        // --- Output ---
        // Write to I2S DMA buffer (will block if buffer is full, regulating speed)
        i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
    }
}
