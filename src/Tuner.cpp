#include "Tuner.h"

// Note Frequencies for lookup (Partial list or formula)
// We will use formula: NoteNum = 12 * log2(freq / 440) + 69  (MIDI standard)
const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

Tuner::Tuner() {
    // FFT object init if needed
}

void Tuner::begin() {
    if (_initialized) return;

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = MIC_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // INMP441 uses 32-bit slots (24-bit data)
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // Or RIGHT depending on connection. ONLY_LEFT is standard for mono INMP441
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_MIC_SCK,
        .ws_io_num = I2S_MIC_WS,
        .data_out_num = -1,
        .data_in_num = I2S_MIC_SD
    };

    // Use I2S_NUM_1 for Input (Output is on NUM_0)
    i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_1);
    
    _initialized = true;
}

int32_t Tuner::getAmplitude() {
    if (!_initialized) return 0;

    size_t bytes_read;
    // Read a smaller chunk for responsiveness
    int samples_to_read = 256; 
    int32_t buffer[samples_to_read];

    i2s_read(I2S_NUM_1, (void*)buffer, samples_to_read * sizeof(int32_t), &bytes_read, 0); // Non-blocking/Immediate return if possible? using 0 wait

    int32_t maxAmp = 0;
    for (int i=0; i < samples_to_read; i++) {
        int32_t val = abs(buffer[i] >> 8); // Remove some LSB noise / align 24bit
        if (val > maxAmp) maxAmp = val;
    }
    return maxAmp;
}

void Tuner::stop() {
    if (_initialized) {
        i2s_driver_uninstall(I2S_NUM_1);
        _initialized = false;
    }
}

float Tuner::getFrequency() {
    if (!_initialized) return 0;
    
    size_t bytes_read;
    // Read raw samples
    i2s_read(I2S_NUM_1, (void*)i2s_raw_buffer, FFT_SAMPLES * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    
    // Convert to double for FFT & Apply Window
    // Also basic noise gate + AGC
    double sum = 0;
    double rms = 0;
    
    for (int i = 0; i < FFT_SAMPLES; i++) {
        // INMP441 is 24-bit left aligned in 32-bit container. 
        // Raw value needs shifting or just treating as big int.
        // Actually usually standard I2S reads correctly into int32.
        
        int32_t val = i2s_raw_buffer[i];
        // Reduce amplitude?
        val = val >> 14; // Scaling down

        // AGC: track RMS and apply gain
        rms += (double)val * (double)val;
        double amplified = (double)val * (double)_agcGain;
        // prevent runaway
        if (amplified > 8388607.0) amplified = 8388607.0;
        if (amplified < -8388608.0) amplified = -8388608.0;

        vReal[i] = amplified;
        vImag[i] = 0;
        sum += abs(val);
    }

    rms = sqrt(rms / FFT_SAMPLES);
    if (rms > 1.0) {
        // adjust gain gently toward target
        const float target = 8000.0f;
        float desired = target / (float)rms;
        // smooth the change to avoid pumping
        _agcGain = _agcGain * 0.9f + desired * 0.1f;
        if (_agcGain < 0.01f) _agcGain = 0.01f;
        if (_agcGain > 64.0f) _agcGain = 64.0f;
    }
    
    // Noise Gate
    if ((sum / FFT_SAMPLES) < NOISE_THRESHOLD) {
        return 0; // Too quiet
    }
    
    // Perform FFT
    // Note: arduinoFFT v1.x API
    FFT = arduinoFFT(vReal, vImag, FFT_SAMPLES, MIC_SAMPLE_RATE);
    FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.Compute(FFT_FORWARD);
    FFT.ComplexToMagnitude();
    
    double peak = FFT.MajorPeak(vReal, FFT_SAMPLES, MIC_SAMPLE_RATE);
    return (float)peak;
}

String Tuner::getNote(float frequency, int &cents) {
    if (frequency < 20) {
        cents = 0;
        return "--";
    }

    // MIDI Note Calc
    // n = 12 * log2(f / 440) + 69
    float noteNumFloat = 12.0 * log(frequency / _a4Ref) / log(2.0) + 69.0;
    int noteNum = round(noteNumFloat);
    
    // Cents offset
    // 100 * (noteNumFloat - noteNum)
    cents = (int)((noteNumFloat - noteNum) * 100);
    
    // Map MIDI to Name
    // C = 0, ...
    int octave = (noteNum / 12) - 1;
    int noteIndex = noteNum % 12;
    if (noteIndex < 0) noteIndex = 0; // safety
    
    String res = String(noteNames[noteIndex]) + String(octave);
    return res;
}

float Tuner::readLevel() {
    if (!_initialized) return 0;
    size_t bytes_read;
    const int block = 256;
    int32_t buf[block];
    if (i2s_read(I2S_NUM_1, (void*)buf, block * sizeof(int32_t), &bytes_read, 0) != ESP_OK) {
        return 0;
    }
    int samples = bytes_read / sizeof(int32_t);
    double rms = 0;
    for (int i = 0; i < samples; i++) {
        int32_t v = buf[i] >> 14;
        double amplified = (double)v * (double)_agcGain;
        rms += amplified * amplified;
    }
    if (samples == 0) return 0;
    return sqrt(rms / samples);
}
