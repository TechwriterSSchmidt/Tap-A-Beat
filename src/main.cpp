#include <Arduino.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <Wire.h>
#include <Preferences.h>
#include <driver/ledc.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "AudioEngine.h"
#include "Tuner.h"

// --- Global Objects ---------------------------------------------------------
U8G2_SH1107_128X128_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ I2C_SCL_PIN, /* data=*/ I2C_SDA_PIN);
ESP32Encoder encoder;
AudioEngine audio;
Tuner tuner;
Preferences prefs;
Adafruit_NeoPixel pixels(WS2812_NUM_LEDS, WS2812_PIN, NEO_GRB + NEO_KHZ800);

// --- State Management -------------------------------------------------------
enum AppState {
    STATE_METRONOME,
    STATE_MENU,
    STATE_TUNER,
    STATE_SET_TIME_SIG, // Submenu to pick 4/4, 3/4 etc
};

AppState currentState = STATE_METRONOME;

// --- Metronome Logic --------------------------------------------------------
struct MetronomeState {
    int bpm = 120;
    bool isPlaying = false;
    unsigned long lastBeatTime = 0;
    int beatCounter = 0; // 0 = first beat (Accent)
    
    // Time Signature
    int beatsPerBar = 4; // Top number (4/4 -> 4)
    
    void resetBeat() {
        beatCounter = 0;
        lastBeatTime = millis();
    }
} metronome;

// --- Menu Logic -------------------------------------------------------------
const char* menuItems[] = {"Time Signature", "Tuner", "Load Preset", "Save Preset", "Exit"};
int menuSelection = 0;
int menuCount = 5;

enum PresetMode {
    PRESET_LOAD,
    PRESET_SAVE
};

int presetSlot = 0; // 0 to NUM_PRESETS-1
PresetMode presetMode = PRESET_LOAD;

// --- Encoder State ----------------------------------------------------------
long lastEncoderValue = 0;
unsigned long buttonPressTime = 0;
bool buttonActive = false;
bool isVolumeAdjustment = false;
bool isTunerToneOn = false;
bool buttonStableState = false;
bool buttonLastRead = false;
unsigned long buttonLastChange = 0;

// Haptics & Visuals
bool hapticEnabled = true;
unsigned long feedbackOffAt = 0;
int hapticNormalDuty = 400; // 10-bit duty (0-1023)
int hapticAccentDuty = 700;
int feedbackPulseMs = 40;

// Tap tempo via mic
unsigned long lastMicTap = 0;
int micTapIntervals[4] = {0,0,0,0};
int micTapIndex = 0;
float micTapThreshold = 12000.0f;
unsigned long micTapDebounceMs = 220;

// Settings persistence
float a4Reference = 440.0f;

// --- Power Management -------------------------------------------------------
unsigned long lastActivityTime = 0;

void enterDeepSleep() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(30, 64, "Good Bye!");
    u8g2.sendBuffer();
    
    // LEDs Off
    pixels.clear();
    pixels.show();
    
    // Stop Audio/Tuner
    audio.stopTone(); 
    tuner.stop();
    
    delay(1000);
    u8g2.setPowerSave(1); // Screen off

    // Wakeup Config
    // ESP32-S3: Use EXT1 for GPIO wakeup from deep sleep
    uint64_t mask = (1ULL << ENC_BUTTON);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

    Serial.println("Entering Deep Sleep...");
    esp_deep_sleep_start();
}

// --- Helper Functions -------------------------------------------------------
void drawMetronomeScreen();
void drawMenuScreen();
void drawPresetScreen();
void saveSettings();
void loadSettings();
void savePreset(int slot);
void loadPreset(int slot);
void drawTunerScreen(float freq, String note, int cents);

// --- Setup ------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000); 

    // Hardware Init
    audio.begin();
    u8g2.begin();
    tuner.begin(); 
    
    pixels.begin();
    pixels.setBrightness(200);
    pixels.clear();
    pixels.show();
    
    ESP32Encoder::useInternalWeakPullResistors = UP;
    encoder.attachHalfQuad(ENC_PIN_A, ENC_PIN_B);
    encoder.setCount(metronome.bpm * 2);
    pinMode(ENC_BUTTON, INPUT_PULLUP);

    // Haptic PWM init (LEDC)
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = HAPTIC_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = HAPTIC_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t)HAPTIC_PWM_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
    
    // Callback handles Haptic + Visuals
    audio.setBeatCallback([](bool accent){
        // 1. Haptic
        if (hapticEnabled) {
            int duty = accent ? hapticAccentDuty : hapticNormalDuty;
            ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH);
        }
        
        // 2. Visual (WS2812)
        if (accent) {
            pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red
        } else {
            pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Blue
        }
        pixels.show();

        feedbackOffAt = millis() + feedbackPulseMs;
    });

    prefs.begin("takt", false);
    loadSettings();
    tuner.setA4Reference(a4Reference);
    lastEncoderValue = encoder.getCount() / 2;
    
    lastActivityTime = millis();
    Serial.println("Tap-A-Beat Ready.");
}

// --- Loop -------------------------------------------------------------------
void loop() {
    unsigned long now = millis();

    // --- Input Handling -----------------------------------------------------
    long newEncVal = encoder.getCount() / 2;
    long delta = newEncVal - lastEncoderValue;
    
    if (delta != 0) lastActivityTime = now;

    // Button Debounce
    bool rawBtn = (digitalRead(ENC_BUTTON) == LOW);
    if (rawBtn != buttonLastRead) {
        buttonLastRead = rawBtn;
        buttonLastChange = now;
    }
    if ((now - buttonLastChange) > 20) {
        if (buttonStableState != rawBtn) {
            buttonStableState = rawBtn;
            if (buttonStableState) {
                buttonActive = true;
                buttonPressTime = now;
            } else {
                buttonActive = false;
                long duration = now - buttonPressTime;
                isVolumeAdjustment = false;
                if (duration < 500) { // Short Click
                    if (currentState == STATE_METRONOME) {
                        metronome.isPlaying = !metronome.isPlaying;
                        if (metronome.isPlaying) metronome.resetBeat();
                        saveSettings();
                    } else if (currentState == STATE_MENU) {
                        if (menuSelection == 0) { // Time Sig
                            metronome.beatsPerBar++;
                            if (metronome.beatsPerBar > 7) metronome.beatsPerBar = 2;
                            currentState = STATE_METRONOME;
                            saveSettings();
                        } else if (menuSelection == 1) { // Tuner
                            currentState = STATE_TUNER;
                        } else if (menuSelection == 2) { // Load
                            presetMode = PRESET_LOAD;
                            currentState = STATE_SET_TIME_SIG;
                        } else if (menuSelection == 3) { // Save
                            presetMode = PRESET_SAVE;
                            currentState = STATE_SET_TIME_SIG;
                        } else if (menuSelection == 4) { // Exit
                            currentState = STATE_METRONOME;
                        }
                    } else if (currentState == STATE_TUNER) {
                        isTunerToneOn = !isTunerToneOn;
                        if (isTunerToneOn) {
                            audio.startTone(a4Reference);
                        } else {
                            audio.stopTone();
                        }
                    } else if (currentState == STATE_SET_TIME_SIG) { // Actually Preset Select
                        if (presetMode == PRESET_LOAD) {
                            loadPreset(presetSlot);
                        } else {
                            savePreset(presetSlot);
                        }
                        currentState = STATE_METRONOME;
                    }
                } else { // Long press
                    if (currentState == STATE_METRONOME) {
                        currentState = STATE_MENU;
                        metronome.isPlaying = false;
                    } else if (currentState == STATE_TUNER) {
                        isTunerToneOn = false;
                        audio.stopTone();
                        currentState = STATE_MENU;
                    } else if (currentState == STATE_SET_TIME_SIG) {
                        currentState = STATE_METRONOME;
                    }
                }
            }
        }
    }
    if (buttonStableState) lastActivityTime = now; 
    
    // Press + Turn (Volume)
    if (buttonActive && delta != 0 && currentState == STATE_METRONOME) {
        isVolumeAdjustment = true;
        int newVol = audio.getVolume() + delta * 2; 
        if (newVol < 0) newVol = 0; 
        if (newVol > 100) newVol = 100;
        audio.setVolume(newVol);
        saveSettings();
        delta = 0; 
        encoder.setCount(lastEncoderValue * 2); 
        newEncVal = lastEncoderValue;
    }

    // Mic Tap Tempo
    if (currentState == STATE_METRONOME && !isTunerToneOn) {
        float lvl = tuner.readLevel();
        if (lvl > micTapThreshold && (now - lastMicTap) > micTapDebounceMs) {
            if (lastMicTap != 0) {
                unsigned long interval = now - lastMicTap;
                micTapIntervals[micTapIndex % 4] = (int)interval;
                micTapIndex++;
                int count = min(micTapIndex, 4);
                long sum = 0;
                for (int i = 0; i < count; i++) sum += micTapIntervals[i];
                int avg = sum / count;
                if (avg > 100 && avg < 2000) {
                    metronome.bpm = 60000 / avg;
                    if (metronome.bpm < 30) metronome.bpm = 30;
                    if (metronome.bpm > 300) metronome.bpm = 300;
                    encoder.setCount(metronome.bpm * 2);
                    saveSettings();
                }
            }
            lastMicTap = now;
        }
    }

    // --- State Logic --------------------------------------------------------
    switch (currentState) {
        case STATE_METRONOME:
            if (delta != 0 && !isVolumeAdjustment) {
                metronome.bpm += delta;
                if (metronome.bpm < 30) metronome.bpm = 30;
                if (metronome.bpm > 300) metronome.bpm = 300;
                lastEncoderValue = newEncVal; 
                saveSettings();
            } 
            
            if (metronome.isPlaying) {
                long interval = 60000 / metronome.bpm;
                if (now - metronome.lastBeatTime >= interval) {
                    metronome.lastBeatTime = now;
                    bool isAccent = (metronome.beatCounter == 0);
                    // This triggers the audio task AND callback for LED/Haptic
                    audio.playClick(isAccent);
                    
                    metronome.beatCounter++;
                    if (metronome.beatCounter >= metronome.beatsPerBar) {
                        metronome.beatCounter = 0;
                    }
                }
            }
            break;

        case STATE_MENU:
            if (delta != 0) {
                menuSelection += delta;
                if (menuSelection < 0) menuSelection = 0;
                if (menuSelection >= menuCount) menuSelection = menuCount - 1;
                lastEncoderValue = newEncVal;
            }
            break;
        case STATE_SET_TIME_SIG:
            if (delta != 0) {
                presetSlot += delta;
                if (presetSlot < 0) presetSlot = 0;
                if (presetSlot >= NUM_PRESETS) presetSlot = NUM_PRESETS - 1;
                lastEncoderValue = newEncVal;
            }
            break;
            
        case STATE_TUNER:
            if (isTunerToneOn) {
                if (delta != 0) {
                    a4Reference += delta;
                    if (a4Reference < 400) a4Reference = 400;
                    if (a4Reference > 480) a4Reference = 480;
                    audio.startTone(a4Reference);
                    tuner.setA4Reference(a4Reference);
                    saveSettings();
                    lastEncoderValue = newEncVal;
                }
            }
            break;
    }


    // --- Drawing ------------------------------------------------------------
    u8g2.clearBuffer();
    switch (currentState) {
        case STATE_METRONOME: drawMetronomeScreen(); break;
        case STATE_MENU: drawMenuScreen(); break;
        case STATE_SET_TIME_SIG: drawPresetScreen(); break;
        case STATE_TUNER:
             if (isTunerToneOn) {
                 drawTunerScreen(a4Reference, "A4", 0);
             } else {
                 float f = tuner.getFrequency();
                 int cents = 0;
                 String n = tuner.getNote(f, cents);
                 drawTunerScreen(f, n, cents); 
             }
             break;
    }
    u8g2.sendBuffer();

    // Haptic/LED Off Timer
    if (feedbackOffAt && now > feedbackOffAt) {
        // Haptic Off
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH);
        
        // LED Off
        pixels.setPixelColor(0, 0);
        pixels.show();
        
        feedbackOffAt = 0;
    }

    // Auto Off Check
    if (!metronome.isPlaying && (now - lastActivityTime > AUTO_OFF_MS)) {
        enterDeepSleep();
    }
}

// --- Drawing Helpers -------------------------------------------------------
void drawMetronomeScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 10, "-- METRONOME --");
    u8g2.drawLine(0, 12, 128, 12);

    // Header Info
    char buf[32];
    sprintf(buf, "%d BPM", metronome.bpm);
    u8g2.drawStr(0, 28, buf);

    sprintf(buf, "%d/4", metronome.beatsPerBar);
    int w = u8g2.getStrWidth(buf);
    u8g2.drawStr(128 - w, 28, buf);

    // Big Beat Counter
    if (metronome.isPlaying) {
        char beatStr[4];
        sprintf(beatStr, "%d", metronome.beatCounter + 1);
        
        u8g2.setFont(u8g2_font_logisoso42_tn); 
        int strW = u8g2.getStrWidth(beatStr);
        // Center around Y=90
        u8g2.drawStr((128 - strW) / 2, 90, beatStr);

        // Visual Accent Frame
        if (metronome.beatCounter == 0) {
             u8g2.drawRFrame(30, 42, 68, 54, 5); 
        }
    } else {
        u8g2.setFont(u8g2_font_profont12_mf);
        const char* msg = "- READY -";
        int strW = u8g2.getStrWidth(msg);
        u8g2.drawStr((128 - strW)/2, 80, msg);
    }
    
    // Bottom Info
    u8g2.setFont(u8g2_font_profont12_mf);
    sprintf(buf, "Vol: %d%%", audio.getVolume());
    u8g2.drawStr(0, 120, buf);
}

void drawMenuScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 10, "-- MENU --");
    u8g2.drawLine(0, 12, 128, 12);

    for (int i = 0; i < menuCount; i++) {
        int y = 28 + i * 14;
        if (i == menuSelection) {
            u8g2.drawBox(0, y - 10, 128, 12);
            u8g2.setDrawColor(0);
            u8g2.drawStr(4, y, menuItems[i]);
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawStr(4, y, menuItems[i]);
        }
    }
}

void drawPresetScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    const char* title = (presetMode == PRESET_LOAD) ? "Load Preset" : "Save Preset";
    u8g2.drawStr(0, 10, title);
    u8g2.drawLine(0, 12, 128, 12);
    char buf[24];
    sprintf(buf, "Slot: %d", presetSlot + 1);
    u8g2.drawStr(40, 40, buf);
    u8g2.drawStr(10, 80, "Click to Confirm");
}

void drawTunerScreen(float freq, String note, int cents) {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 10, "--- TUNER ---");
    u8g2.drawLine(0, 12, 128, 12);

    if (isTunerToneOn) {
        u8g2.drawStr(80, 10, "[TONE]");
        u8g2.setFont(u8g2_font_profont12_mf);
        char a4buf[24];
        sprintf(a4buf, "A4 = %.1fHz", a4Reference);
        u8g2.drawStr(20, 60, a4buf);
        u8g2.drawStr(20, 80, "Playing...");
        return; 
    }

    if (freq < 20) {
        u8g2.drawStr(40, 60, "Listening...");
        return;
    }

    u8g2.setFont(u8g2_font_logisoso42_tn);
    int w = u8g2.getStrWidth(note.c_str());
    u8g2.drawStr((128 - w) / 2, 60, note.c_str());
    
    u8g2.setFont(u8g2_font_profont12_mf);
    char buf[16];
    sprintf(buf, "%d Hz", (int)freq);
    u8g2.drawStr((128 - u8g2.getStrWidth(buf))/2, 80, buf);

    int x = 64 + (cents * 1.2); 
    if (x < 2) x = 2;
    if (x > 126) x = 126;
    
    u8g2.drawFrame(4, 95, 120, 10);
    u8g2.drawLine(64, 92, 64, 108); 
    u8g2.drawBox(x-2, 95, 4, 10); 

    if (cents < -5) u8g2.drawStr(10, 90, "FLAT");
    else if (cents > 5) u8g2.drawStr(90, 90, "SHARP");
    else u8g2.drawStr(50, 90, "* OK *");
}

void saveSettings() {
    prefs.putInt("bpm", metronome.bpm);
    prefs.putInt("ts", metronome.beatsPerBar);
    prefs.putInt("vol", audio.getVolume());
    prefs.putFloat("a4", a4Reference);
}

void loadSettings() {
    metronome.bpm = prefs.getInt("bpm", 120);
    metronome.beatsPerBar = prefs.getInt("ts", 4);
    int vol = prefs.getInt("vol", 50);
    a4Reference = prefs.getFloat("a4", 440.0f);
    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    audio.setVolume(vol);
    tuner.setA4Reference(a4Reference);
    encoder.setCount(metronome.bpm * 2);
}

void savePreset(int slot) {
    char key[16];
    sprintf(key, "p%d_bpm", slot);
    prefs.putInt(key, metronome.bpm);
    sprintf(key, "p%d_ts", slot);
    prefs.putInt(key, metronome.beatsPerBar);
    sprintf(key, "p%d_vol", slot);
    prefs.putInt(key, audio.getVolume());
    sprintf(key, "p%d_a4", slot);
    prefs.putFloat(key, a4Reference);
}

void loadPreset(int slot) {
    char key[16];
    sprintf(key, "p%d_bpm", slot);
    metronome.bpm = prefs.getInt(key, metronome.bpm);
    sprintf(key, "p%d_ts", slot);
    metronome.beatsPerBar = prefs.getInt(key, metronome.beatsPerBar);
    sprintf(key, "p%d_vol", slot);
    int vol = prefs.getInt(key, audio.getVolume());
    sprintf(key, "p%d_a4", slot);
    a4Reference = prefs.getFloat(key, a4Reference);
    tuner.setA4Reference(a4Reference);
    audio.setVolume(vol);
    encoder.setCount(metronome.bpm * 2);
    saveSettings();
}

