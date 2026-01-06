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
        .dma_buf_len = 64
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
    _overdrive = false;
    return o;
}

void AudioEngine::stopTone() {
    _isTonePlaying = false;
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void AudioEngine::startTone(float frequency) {
    _toneFreq = frequency;
    _isTonePlaying = true;
}

void AudioEngine::updateTone() {
    if (!_isTonePlaying) return;

    // Check if we can write? 
    // Actually i2s_write blocks if buffer full.
    // We just write small chunks to keep it full without blocking main loop too long.
    // 64 samples at 44.1k is 1.4ms. 
    
    size_t bytes_written;
    int chunkSamples = 128; // ~3ms
    int16_t samples_data[chunkSamples * 2];

    float phaseIncrement = (2.0f * PI * _toneFreq) / (float)SAMPLE_RATE;
    float volFactor = (float)_volume / 100.0f;

    // Simple soft start/stop envelope (short fade avoids clicks) and limiter
    float fadeLen = 0.005f; // 5 ms fade window
    float samplesPerFade = SAMPLE_RATE * fadeLen;
    for (int j = 0; j < chunkSamples; j++) {
        float env = 1.0f;
        if (j < samplesPerFade) {
            env = (float)j / samplesPerFade;
        } else if (j > chunkSamples - samplesPerFade) {
            env = (float)(chunkSamples - j) / samplesPerFade;
            if (env < 0) env = 0;
        }

        float val = sin(_tonePhase) * 32767.0f * volFactor * env;
        int16_t s = applyLimiter((int32_t)val);
        samples_data[j*2] = s;
        samples_data[j*2+1] = s;
        
        _tonePhase += phaseIncrement;
        if (_tonePhase > 2.0f * PI) _tonePhase -= 2.0f * PI;
    }
    
    // Non-blocking write attempt? No, standard API is blocking.
    // We only write ONE chunk per loop call. If loop is fast, buffer stays full.
    // If loop is slow, audio might glitch/stutter.
    // For a metronome TUNER mode, loop is mainly drawing screen, so it should be fine.
    
    i2s_write(I2S_NUM_0, samples_data, chunkSamples * 2 * sizeof(int16_t), &bytes_written, 0); 
    // Timeout 0 = return immediately if full.
}

void AudioEngine::playClick(bool isAccent) {
    // Sound Parameters
    float freq = isAccent ? 1500.0f : 800.0f; // High pitch for '1', Low for others
    int durationMs = 40; // Short precise click
    
    generateWave(freq, durationMs, (float)_volume / 100.0f);
    if (_beatCallback) _beatCallback(isAccent);
}

void AudioEngine::generateWave(float frequency, int durationMs, float amplitude) {
    if (amplitude <= 0.01f) return; // Silent

    size_t bytes_written;
    int samples = (SAMPLE_RATE * durationMs) / 1000;
    
    // We create a temporary buffer. 16bit stereo = 4 bytes per sample
    // Allocating on stack for short sounds is risky if stack is small, but 40ms * 44100 = 1764 samples * 4 bytes = ~7KB.
    // Better to allocate on heap or chunks. Let's do small chunks to avoid blocking too long.
    
    int chunkSize = 256; 
    int16_t samples_data[chunkSize * 2]; // Stereo

    float phase = 0;
    float phaseIncrement = (2.0f * PI * frequency) / (float)SAMPLE_RATE;
    
    for (int i = 0; i < samples; i += chunkSize) {
        int currentChunk = (samples - i > chunkSize) ? chunkSize : (samples - i);
        
        for (int j = 0; j < currentChunk; j++) {
            // Apply a fast exponential decay envelope for percussive sound
            // t goes from 0 to 1 over the duration of the whole sound
            float t = (float)(i + j) / (float)samples;
            float envelope = pow(1.0f - t, 4.0f); // Fast decay
            
            float val = sin(phase) * 32767.0f * amplitude * envelope;
            int16_t s = applyLimiter((int32_t)val);
            
            samples_data[j*2] = s;     // Left
            samples_data[j*2+1] = s;   // Right
            
            phase += phaseIncrement;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
        }
        
        i2s_write(I2S_NUM_0, samples_data, currentChunk * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    }
    
    // IMPORTANT: Write some silence to ensure the buffer is flushed out and the amp doesn't click
    // or just let it be handled by the next call. Actually, I2S driver holds output.
    // We add a tiny bit of silence fading to 0? The envelope already went to ~0.
    
    i2s_zero_dma_buffer(I2S_NUM_0); // Clear remaining buffer parts?? No, that might cut off sound.
    // Ideally we just leave it. The amp might hum if we don't zero.
    // Let's write a tiny buffer of zeros.
     int16_t silence[64] = {0};
     i2s_write(I2S_NUM_0, silence, sizeof(silence), &bytes_written, 10);
}

int16_t AudioEngine::applyLimiter(int32_t sample) {
    const int32_t softLimit = 28000; // ~-1 dBFS
    if (sample > softLimit) {
        _overdrive = true;
        sample = softLimit + (sample - softLimit) / 4; // soft knee
    } else if (sample < -softLimit) {
        _overdrive = true;
        sample = -softLimit + (sample + softLimit) / 4;
    }

    // Final hard clamp to int16 range
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    return (int16_t)sample;
}
