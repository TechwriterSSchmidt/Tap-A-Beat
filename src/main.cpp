#include <Arduino.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <Wire.h>
#include <Preferences.h>
#include <driver/ledc.h>
#include "config.h"
#include "AudioEngine.h"
#include "Tuner.h"

// --- Global Objects ---------------------------------------------------------
U8G2_SH1107_128X128_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ I2C_SCL_PIN, /* data=*/ I2C_SDA_PIN);
ESP32Encoder encoder;
AudioEngine audio;
Tuner tuner;
Preferences prefs;

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
    // Note value (bottom) is usually assumed quarter for metronomes, 
    // unless we get fancy with compound time.
    
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

int presetSlot = 0; // 0-2
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

// Haptics
bool hapticEnabled = true;
unsigned long hapticOffAt = 0;
int hapticNormalDuty = 400; // 10-bit duty (0-1023)
int hapticAccentDuty = 700;
int hapticPulseMs = 40;

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
    delay(1000);
    
    u8g2.setPowerSave(1); // Screen off
    
    // Stop Audio
    audio.stopTone(); 
    // Tuner stop
    tuner.stop();

    // Config Wakeup on Button (Pin 6 = ENC_BUTTON) Low
    // ESP32-S3 Ext1 Wakeup
    // Mask for GPIO 6: 1 << 6
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
// (Redefine drawTunerScreen)

void drawTunerScreen(float freq, String note, int cents) {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 10, "--- TUNER ---");
    u8g2.drawLine(0, 12, 128, 12);

    // Mode Status
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

    // Note Name
    u8g2.setFont(u8g2_font_logisoso42_tn);
    int w = u8g2.getStrWidth(note.c_str());
    u8g2.drawStr((128 - w) / 2, 60, note.c_str());
    
    // Arrows / Cent Bar
    u8g2.setFont(u8g2_font_profont12_mf);
    char buf[16];
    sprintf(buf, "%d Hz", (int)freq);
    u8g2.drawStr((128 - u8g2.getStrWidth(buf))/2, 80, buf);

    // Cent Visual
    // Center at 64. Scale: +/- 50 cents = +/- 60 pixels
    int x = 64 + (cents * 1.2); 
    if (x < 2) x = 2;
    if (x > 126) x = 126;
    
    u8g2.drawFrame(4, 95, 120, 10);
    u8g2.drawLine(64, 92, 64, 108); // Center MArker
    u8g2.drawBox(x-2, 95, 4, 10); // Indicator cursor

    // Arrows
    if (cents < -5) {
         u8g2.drawStr(10, 90, "<< FLAT");
    } else if (cents > 5) {
         u8g2.drawStr(90, 90, "SHARP >>");
    } else {
         u8g2.drawStr(50, 90, "* OK *");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000); // safety

    // Hardware Init
    audio.begin();
    u8g2.begin();
    tuner.begin(); // keep mic available for tap tempo
    
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
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH);

    audio.setBeatCallback([](bool accent){
        if (!hapticEnabled) return;
        int duty = accent ? hapticAccentDuty : hapticNormalDuty;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH);
        hapticOffAt = millis() + hapticPulseMs;
    });

    prefs.begin("takt", false);
    loadSettings();
    tuner.setA4Reference(a4Reference);
    lastEncoderValue = encoder.getCount() / 2;
    
    lastActivityTime = millis();
    Serial.println("Takt-O-Beat Ready.");
}

void loop() {
    unsigned long now = millis();

    // --- Input Handling -----------------------------------------------------
    long newEncVal = encoder.getCount() / 2;
    long delta = newEncVal - lastEncoderValue;
    
    // Power Management: Reset timer on activity
    if (delta != 0) lastActivityTime = now;

    // Check Button with debounce
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
                        } else if (menuSelection == 2) { // Load preset
                            presetMode = PRESET_LOAD;
                            currentState = STATE_SET_TIME_SIG;
                        } else if (menuSelection == 3) { // Save preset
                            presetMode = PRESET_SAVE;
                            currentState = STATE_SET_TIME_SIG;
                        } else if (menuSelection == 4) { // Exit
                            currentState = STATE_METRONOME;
                        }
                    } else if (currentState == STATE_TUNER) {
                        // Toggle Reference Tone
                        isTunerToneOn = !isTunerToneOn;
                        if (isTunerToneOn) {
                            audio.startTone(a4Reference);
                        } else {
                            audio.stopTone();
                        }
                    } else if (currentState == STATE_SET_TIME_SIG) {
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
    if (buttonStableState) lastActivityTime = now; // Button held counts as activity
    
    // Detect "Press and Turn" for Volume (immediate action)
    if (buttonActive && delta != 0 && currentState == STATE_METRONOME) {
        isVolumeAdjustment = true;
        
        int newVol = audio.getVolume() + delta * 2; // Faster vol change
        if (newVol < 0) newVol = 0; 
        if (newVol > 100) newVol = 100;
        audio.setVolume(newVol);
        saveSettings();
        
        // Consume the delta so BPM doesn't change
        delta = 0; 
        // Reset encoder match
        encoder.setCount(lastEncoderValue * 2); // Dont move encoder logic pos
        newEncVal = lastEncoderValue;
    }

    // tap tempo via mic when in metronome mode
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

    // --- Logic based on State -----------------------------------------------
    
    switch (currentState) {
        case STATE_METRONOME:
            if (delta != 0 && !isVolumeAdjustment) {
                metronome.bpm += delta;
                if (metronome.bpm < 30) metronome.bpm = 30;
                if (metronome.bpm > 300) metronome.bpm = 300;
                lastEncoderValue = newEncVal; // Update tracking
                saveSettings();
            } else if (isVolumeAdjustment) {
                // logic handled in button section
            }
            
            // Metronome Timing
            if (metronome.isPlaying) {
                long interval = 60000 / metronome.bpm;
                if (now - metronome.lastBeatTime >= interval) {
                    metronome.lastBeatTime = now;
                    // Audio Trigger
                    bool isAccent = (metronome.beatCounter == 0);
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
                if (presetSlot > 2) presetSlot = 2;
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
                audio.updateTone();
            } else {
                // Tuner Active
                // We just read in draw loop or here?
                // Read here is better
            }
            break;
    }


    // --- Drawing ------------------------------------------------------------
    u8g2.clearBuffer();
    
    switch (currentState) {
        case STATE_METRONOME:
            drawMetronomeScreen();
            break;
        case STATE_MENU:
            drawMenuScreen();
            break;
        case STATE_SET_TIME_SIG:
            drawPresetScreen();
            break;
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
        default:
            break;
    }
    
    u8g2.sendBuffer();

    // Haptic pulse off when elapsed
    if (hapticEnabled && hapticOffAt && now > hapticOffAt) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)HAPTIC_PWM_CH);
        hapticOffAt = 0;
    }

    // --- Auto Off Check -----------------------------------------------------
    // Sleep if inactive for AUTO_OFF_MS AND metronome is NOT playing
    if (!metronome.isPlaying && (now - lastActivityTime > AUTO_OFF_MS)) {
        // Tuner mode is not "playing", so it will sleep after 2 mins of no button/knob use
        // which is good behavior.
        enterDeepSleep();
    }
}

// --- Drawing Helpers -------------------------------------------------------
void drawMetronomeScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 10, "-- METRONOME --");
    u8g2.drawLine(0, 12, 128, 12);

    char buf[24];
    sprintf(buf, "BPM: %d", metronome.bpm);
    u8g2.drawStr(0, 30, buf);

    sprintf(buf, "Time: %d/4", metronome.beatsPerBar);
    u8g2.drawStr(0, 45, buf);

    sprintf(buf, "Vol: %d%%", audio.getVolume());
    u8g2.drawStr(0, 60, buf);

    const char* status = metronome.isPlaying ? "Playing" : "Stopped";
    u8g2.drawStr(0, 75, status);

    // Simple beat indicator bar
    int barWidth = 120;
    int beatPos = (metronome.beatCounter % metronome.beatsPerBar);
    int markerX = 4 + (barWidth * beatPos) / max(1, metronome.beatsPerBar - 1);
    u8g2.drawFrame(4, 100, barWidth, 10);
    u8g2.drawBox(markerX, 100, 6, 10);
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
    u8g2.drawStr(0, 30, buf);
    u8g2.drawStr(0, 50, "Click = confirm");
    u8g2.drawStr(0, 65, "Long = cancel");
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

