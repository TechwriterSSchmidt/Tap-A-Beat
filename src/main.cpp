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
// Check config.h for pins. Using HW I2C for Speed.
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
    STATE_AM_TIME_SIG, // Adjustment Menu: Time Signature
    STATE_AM_BPM,      // Adjustment Menu: BPM (via Menu)
    STATE_TAP_TEMPO,   // New Tap State
    STATE_PRESET_SELECT // Loading/Saving
};

AppState currentState = STATE_METRONOME;
bool isVolumeFocus = false; // Toggle between BPM (false) and Volume (true) adjustment

// --- Metronome Logic --------------------------------------------------------
struct MetronomeState {
    volatile int bpm = 120;
    volatile bool isPlaying = false;
    unsigned long lastBeatTime = 0;
    volatile int beatCounter = 0; // 0 = first beat (Accent)
    volatile int beatsPerBar = 4; // Top number (4/4 -> 4)

    void resetBeat() {
        beatCounter = 0;
        lastBeatTime = millis();
    }
} metronome;

TaskHandle_t metronomeTaskHandle = NULL;

// --- Menu Logic -------------------------------------------------------------
// Removed "Speed (BPM)" because it's editable in Home view
const char* menuItems[] = {"Metric", "Tap Tempo", "Tuner", "Load Preset", "Save Preset", "Exit"};
int menuSelection = 0;
int menuCount = 6;

enum PresetMode {
    PRESET_LOAD,
    PRESET_SAVE
};
int presetSlot = 0; // 0 to NUM_PRESETS-1
PresetMode presetMode = PRESET_LOAD;
#define NUM_PRESETS 5

// --- Encoder / Input State --------------------------------------------------
long lastEncoderValue = 0;
unsigned long buttonPressTime = 0;
bool buttonActive = false;
bool buttonStableState = false;
bool buttonLastRead = false;
unsigned long buttonLastChange = 0;

// Tap Tempo Globals
float tapSensitivity = 0.5f; // 0.0 to 1.0
float tapInputLevel = 0.0f;
unsigned long lastTapTime = 0;
unsigned long tapIntervalAccumulator = 0;
int tapCount = 0;
bool showTapVisual = false;
unsigned long tapVisualStartTime = 0;
#define TAP_TIMEOUT 2000 // Reset tap sequence after 2s silence

// Haptics & Visuals
bool hapticEnabled = true;
unsigned long feedbackOffAt = 0;
int hapticNormalDuty = 400; // 10-bit duty (0-1023)
int hapticAccentDuty = 700;
int feedbackPulseMs = 40;

// Settings persistence
float a4Reference = 440.0f;
int tempBPM = 120; // For Adjust BPM Screen
bool isTunerToneOn = false;

// --- Power Management -------------------------------------------------------
unsigned long lastActivityTime = 0;

// --- Forward Declarations ---------------------------------------------------
void drawMetronomeScreen();
void drawMenuScreen();
void drawTunerScreen(float freq, String note, int cents);
void drawTimeSigScreen();
void drawBPMScreen(); 
void drawTapScreen();
void drawPresetScreen();
void enterDeepSleep();
void saveSettings();
void loadSettings();
void savePreset(int slot);
void loadPreset(int slot);

// --- Audio Task (High Precision Metronome Trigger on Core 0) ----------------
void metronomeTask(void * parameter) {
    unsigned long lastBeat = millis();
    
    for(;;) {
        if (metronome.isPlaying && currentState == STATE_METRONOME) {
            unsigned long interval = 60000 / metronome.bpm;
            unsigned long now = millis();
            
            if (now - lastBeat >= interval) {
                lastBeat = now;
                
                // Play Click
                bool isAccent = (metronome.beatCounter == 0);
                audio.playClick(isAccent);
                
                // Advance Beat
                metronome.beatCounter++;
                if (metronome.beatCounter >= metronome.beatsPerBar) {
                    metronome.beatCounter = 0;
                }
            }
        } else {
             if (!metronome.isPlaying) {
                 metronome.beatCounter = 0;
             }
             lastBeat = millis(); // Reset reference
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS); // Yield
    }
}

// --- Setup ------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(100); 

    // Hardware Init
    audio.begin();
    u8g2.begin();
    tuner.begin();
    
    // Pixels
    pixels.begin();
    pixels.setBrightness(200);
    pixels.clear();
    pixels.show();

    // Preferences Init
    prefs.begin("taktobeat", false);
    loadSettings();
    
    ESP32Encoder::useInternalWeakPullResistors = UP;
    encoder.attachHalfQuad(ENC_PIN_A, ENC_PIN_B);
    encoder.setCount(metronome.bpm * 2);
    lastEncoderValue = encoder.getCount() / 2;
    pinMode(ENC_BUTTON, INPUT_PULLUP);
    
    // Haptic PWM init (LEDC Legacy API for Core 2.0.x)
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

    lastActivityTime = millis();
    
    // Create Audio Task on Core 0
    xTaskCreatePinnedToCore(
      metronomeTask,   "MetronomeTask", 
      4096,        NULL, 
      2,           &metronomeTaskHandle, 
      0            
    );

    Serial.println("Takt-O-Beat Ready.");
}

// --- Main Loop --------------------------------------------------------------
void loop() {
    unsigned long now = millis();

    // 1. Input Handling
    long newEncVal = encoder.getCount() / 2;
    long delta = newEncVal - lastEncoderValue;
    
    if (delta != 0) lastActivityTime = now;

    // Button
    bool rawBtn = (digitalRead(ENC_BUTTON) == LOW);
    if (rawBtn != buttonLastRead) {
        buttonLastRead = rawBtn;
        buttonLastChange = now;
    }
    
    if ((now - buttonLastChange) > 20) { // Debounce
        if (buttonStableState != rawBtn) {
            buttonStableState = rawBtn;
            if (buttonStableState) { // Press
                buttonActive = true;
                buttonPressTime = now;
                lastActivityTime = now;
            } else { // Release
                buttonActive = false;
                long duration = now - buttonPressTime;
                
                if (duration < 500) { // Short Click
                    // Single Press Handling
                    if (currentState == STATE_METRONOME) {
                        // Toggle Focus between BPM and Volume
                        isVolumeFocus = !isVolumeFocus;
                        // Reset encoder helper
                        lastEncoderValue = newEncVal; 
                        encoder.setCount(lastEncoderValue * 2);
                        saveSettings();
                    } else if (currentState == STATE_MENU) {
                        if (menuSelection == 0) { // Metric
                             currentState = STATE_AM_TIME_SIG;
                        } else if (menuSelection == 1) { // Tap Tempo
                             currentState = STATE_TAP_TEMPO;
                             tuner.begin(); // Enable mic
                             lastTapTime = 0;
                             tapCount = 0;
                        } else if (menuSelection == 2) { // Tuner
                             currentState = STATE_TUNER;
                             tuner.begin(); 
                        } else if (menuSelection == 3) { // Load
                             currentState = STATE_PRESET_SELECT;
                             presetMode = PRESET_LOAD;
                        } else if (menuSelection == 4) { // Save
                             currentState = STATE_PRESET_SELECT;
                             presetMode = PRESET_SAVE;
                        } else if (menuSelection == 5) { // Exit
                             currentState = STATE_METRONOME;
                        }
                    } else if (currentState == STATE_TUNER) {
                        isTunerToneOn = !isTunerToneOn;
                        if (isTunerToneOn) {
                            audio.startTone(a4Reference);
                            tuner.stop();
                        } else {
                            audio.stopTone();
                            tuner.begin();
                        }
                    } else if (currentState == STATE_AM_TIME_SIG) {
                        currentState = STATE_MENU;
                        saveSettings();
                    } else if (currentState == STATE_AM_BPM) {
                        currentState = STATE_MENU;
                        metronome.bpm = tempBPM;
                        encoder.setCount(metronome.bpm * 2);
                        saveSettings();
                    } else if (currentState == STATE_TAP_TEMPO) {
                        currentState = STATE_MENU;
                        tuner.stop();
                        saveSettings();
                    } else if (currentState == STATE_PRESET_SELECT) {
                        if (presetMode == PRESET_LOAD) loadPreset(presetSlot);
                        else savePreset(presetSlot);
                        currentState = STATE_MENU;
                    }
                } else { // Long Press (> 500ms)
                    if (currentState == STATE_METRONOME) {
                        // Long Press Strategy: 
                        // > 2000ms: Menu
                        // > 600ms: Play/Stop (Logic below in separate block if release timing allows)
                        // Actually, simplified: Long Press = Menu.
                        // We need a way to Play/Stop. 
                        // I'll add "Play/Stop" to the Menu? Or make it a Double Click later.
                        // For now, let's keep Long Press -> Menu to support all features.
                        // AND I'll add a "Metric" screen click = Play logic? No.
                        
                        // Let's rely on the timed logic below for distinct actions
                        // But here is the Release event. Can't distinguish Very Long from Long easily without start time.
                        if (now - buttonPressTime > 2000) {
                            metronome.isPlaying = false;
                            currentState = STATE_MENU;
                            isVolumeFocus = false;
                        } else {
                            // Medium Press (0.5 - 2s) -> Play/Stop
                             metronome.isPlaying = !metronome.isPlaying;
                             if (metronome.isPlaying) metronome.beatCounter = 0;
                             saveSettings();
                        }

                    } else {
                        // Exit back to Metronome
                        currentState = STATE_METRONOME;
                        isTunerToneOn = false;
                        audio.stopTone();
                        if (currentState == STATE_TUNER || currentState == STATE_TAP_TEMPO) tuner.stop();
                    }
                }
            }
        }
    }
    
    // Encoder State Logic
    if (delta != 0) {
        if (currentState == STATE_METRONOME) {
            if (isVolumeFocus) {
                // Adjust Volume
                int newVol = audio.getVolume() + delta * 2;
                if (newVol < 0) newVol = 0; 
                if (newVol > 100) newVol = 100;
                audio.setVolume(newVol);
            } else {
                // Adjust BPM
                metronome.bpm += delta;
                if (metronome.bpm < 30) metronome.bpm = 30;
                if (metronome.bpm > 300) metronome.bpm = 300;
            }
        } else if (currentState == STATE_MENU) {
            menuSelection += delta;
            if (menuSelection < 0) menuSelection = 0;
            if (menuSelection >= menuCount) menuSelection = menuCount - 1;
        } else if (currentState == STATE_AM_TIME_SIG) {
            metronome.beatsPerBar += delta;
            if (metronome.beatsPerBar < 1) metronome.beatsPerBar = 1;
            if (metronome.beatsPerBar > 12) metronome.beatsPerBar = 12; 
        } else if (currentState == STATE_AM_BPM) {
            tempBPM += delta;
            if (tempBPM < 30) tempBPM = 30;
            if (tempBPM > 300) tempBPM = 300;
        } else if (currentState == STATE_PRESET_SELECT) {
            presetSlot += delta;
            if (presetSlot < 0) presetSlot = 0;
            if (presetSlot >= NUM_PRESETS) presetSlot = NUM_PRESETS - 1;
        } else if (currentState == STATE_TAP_TEMPO) {
            tapSensitivity += (delta * 0.05f);
            if (tapSensitivity < 0.1f) tapSensitivity = 0.1f;
            if (tapSensitivity > 1.0f) tapSensitivity = 1.0f;
        } else if (currentState == STATE_TUNER && isTunerToneOn) {
            a4Reference += delta;
            if (a4Reference < 400) a4Reference = 400;
            if (a4Reference > 480) a4Reference = 480;
            audio.startTone(a4Reference);
            tuner.setA4Reference(a4Reference);
        }
        lastEncoderValue = newEncVal;
    }
    
    // Tap Tempo Analysis
    if (currentState == STATE_TAP_TEMPO) {
        float lvl = tuner.readLevel();
        tapInputLevel = (lvl / 15000000.0f); 
        float threshold = 5000000.0f + (1.0f - tapSensitivity) * 10000000.0f;
        
        if (lvl > threshold) {
             if (now - lastTapTime > 150) { 
                 lastActivityTime = now;
                 if (now - lastTapTime > TAP_TIMEOUT) {
                     tapCount = 1;
                     tapIntervalAccumulator = 0;
                 } else {
                     tapCount++;
                     tapIntervalAccumulator += (now - lastTapTime);
                     if (tapCount >= 2) {
                          float avg = (float)tapIntervalAccumulator / (float)(tapCount - 1);
                          int b = (int)(60000.0f / avg);
                          if (b >= 30 && b <= 300) {
                              metronome.bpm = b;
                              encoder.setCount(metronome.bpm * 2);
                          }
                     }
                 }
                 lastTapTime = now;
             }
        }
    }

    // 2. Drawing
    u8g2.clearBuffer();
    
    switch (currentState) {
        case STATE_METRONOME:
            drawMetronomeScreen();
            break;
        case STATE_MENU:
            drawMenuScreen();
            break;
        case STATE_AM_TIME_SIG:
            drawTimeSigScreen();
            break;
        case STATE_AM_BPM:
            drawBPMScreen();
            break;
        case STATE_TAP_TEMPO:
            drawTapScreen();
            break;
        case STATE_PRESET_SELECT:
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

    // 3. Auto Off
    if (!metronome.isPlaying && currentState != STATE_TUNER && currentState != STATE_TAP_TEMPO && (now - lastActivityTime > AUTO_OFF_MS)) {
        enterDeepSleep();
    }
}

// --- Drawing Implementation -------------------------------------------------

void drawMetronomeScreen() {
    // BPM
    u8g2.setFont(u8g2_font_logisoso42_tn);
    u8g2.setCursor(20, 60);
    
    // Blink/Dim BPM if strictly in Volume Focus? Or just Highlight Volume?
    if (isVolumeFocus) u8g2.setDrawColor(0); // Invert?
    else u8g2.setDrawColor(1);
    
    // Draw Background to indicate "Not Focused" properly
    if (isVolumeFocus) {
         // Maybe just gray text effect (checkered)? No, 1-bit.
    }
    
    u8g2.print(metronome.bpm);
    u8g2.setDrawColor(1); // Restore
    
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(95, 60, "BPM");

    // Beat Visual
    int cx = 64; 
    int cy = 90;
    
    // Tap Visual Overlay
    if (showTapVisual) {
        if (millis() - tapVisualStartTime > 200) showTapVisual = false;
        u8g2.setFont(u8g2_font_logisoso24_tn);
        u8g2.drawStr(35, 110, "TAP!");
        return; 
    }

    // Volume Overlay (when adjusting or focused)
    if (isVolumeFocus) {
        u8g2.setDrawColor(0);
        u8g2.drawBox(14, 40, 100, 50); // Clear area
        u8g2.setDrawColor(1);
        u8g2.drawFrame(14, 40, 100, 50);
        
        u8g2.setFont(u8g2_font_profont12_mf);
        u8g2.drawStr(20, 55, "VOLUME");
        
        u8g2.setFont(u8g2_font_logisoso24_tn);
        u8g2.setCursor(45, 85);
        u8g2.print(audio.getVolume());
        
        // Indicate Click to Return
        u8g2.setFont(u8g2_font_tiny5_tf);
        u8g2.drawStr(30, 88, "Click -> BPM");
        return; // Skip drawing the rest
    }

    if (metronome.isPlaying) {
        u8g2.drawDisc(cx, cy, 10 + (metronome.beatCounter % 2)*4); // Pulse
        
        u8g2.setCursor(45, 115);
        u8g2.print(metronome.beatCounter + 1);
        u8g2.print("/");
        u8g2.print(metronome.beatsPerBar);
    } else {
        u8g2.drawCircle(cx, cy, 10);
        u8g2.setCursor(40, 115);
        u8g2.print("Hold: Play");
    }
    
    // Volume Bar
    int volW = map(audio.getVolume(), 0, 100, 0, 128);
    u8g2.drawBox(0, 124, volW, 4);
}

void drawMenuScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 10, "-- MENU --");
    u8g2.drawLine(0, 12, 128, 12);
    
    int startY = 30;
    int h = 14;
    
    for (int i = 0; i < menuCount; i++) {
        if (i == menuSelection) {
            u8g2.drawBox(0, startY + i*h - 9, 128, 11);
            u8g2.setDrawColor(0);
        } else {
            u8g2.setDrawColor(1);
        }
        
        u8g2.setCursor(4, startY + i*h);
        
        if (i == 0) {
             u8g2.print("Metric: ");
             u8g2.print(metronome.beatsPerBar);
             u8g2.print("/4");
        } else {
             u8g2.print(menuItems[i]);
        }
    }
    u8g2.setDrawColor(1);
}

void drawTimeSigScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 12, "--- TIME SIG ---");
    
    // Big number
    u8g2.setFont(u8g2_font_logisoso42_tn);
    u8g2.setCursor(40, 70);
    u8g2.print(metronome.beatsPerBar);
    
    u8g2.setFont(u8g2_font_profont12_mf);
    if (metronome.beatsPerBar == 1) u8g2.drawStr(30, 90, "No Accent");
    else {
        u8g2.setCursor(30, 90);
        u8g2.print(metronome.beatsPerBar);
        u8g2.print(" Beats/Bar");
    }
    
    // Arrows
    u8g2.drawTriangle(10, 50, 25, 40, 25, 60); // Left
    u8g2.drawTriangle(118, 50, 103, 40, 103, 60); // Right
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
        return; 
    }

    if (freq < 20) {
        u8g2.drawStr(40, 60, "Listening...");
        return;
    }

    // Note Name
    u8g2.setFont(u8g2_font_logisoso32_tf);
    int w = u8g2.getStrWidth(note.c_str());
    u8g2.drawStr((128 - w) / 2, 60, note.c_str());
    
    // Hz
    u8g2.setFont(u8g2_font_profont12_mf);
    char buf[16];
    sprintf(buf, "%d Hz", (int)freq);
    u8g2.drawStr((128 - u8g2.getStrWidth(buf))/2, 80, buf);

    // Cent Bar
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

void drawBPMScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 12, "--- SET SPEED ---");
    
    // Big number
    u8g2.setFont(u8g2_font_logisoso42_tn);
    char buf[8];
    sprintf(buf, "%d", tempBPM);
    int w = u8g2.getStrWidth(buf);
    u8g2.setCursor((128 - w) / 2, 70); 
    u8g2.print(tempBPM);
    
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(54, 90, "BPM");
    
    // Arrows
    u8g2.drawTriangle(10, 50, 25, 40, 25, 60); // Left
    u8g2.drawTriangle(118, 50, 103, 40, 103, 60); // Right
    u8g2.drawStr(25, 110, "Click to Set");
}

void drawTapScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(30, 12, "TAP TEMPO");
    u8g2.drawLine(0, 14, 128, 14);
    
    int cx = 64;
    int cy = 60;
    
    // Simple Heart with Lines
    u8g2.drawLine(cx, cy + 30, cx - 30, cy - 10);
    u8g2.drawLine(cx - 30, cy - 10, cx - 15, cy - 25);
    u8g2.drawLine(cx - 15, cy - 25, cx, cy - 10);
    u8g2.drawLine(cx, cy + 30, cx + 30, cy - 10);
    u8g2.drawLine(cx + 30, cy - 10, cx + 15, cy - 25);
    u8g2.drawLine(cx + 15, cy - 25, cx, cy - 10);
    
    // Fill from level
    int level = (int)(tapInputLevel * 30);
    if (level > 30) level = 30;
    if (level > 0) {
        u8g2.drawTriangle(cx, cy+30, cx - level/2, cy+30-level, cx + level/2, cy+30-level);
    }
    
    char buf[32];
    sprintf(buf, "Sens: %d%%", (int)(tapSensitivity * 100));
    u8g2.drawStr(10, 120, buf);

    if (tapCount > 1) {
        sprintf(buf, "BPM: %d", metronome.bpm);
        u8g2.drawStr(70, 120, buf);
    } else {
         u8g2.drawStr(70, 120, "TAP NOW!");
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

void enterDeepSleep() {
    saveSettings();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(30, 64, "Good Bye!");
    u8g2.sendBuffer();
    delay(500);
    
    // LEDs Off
    pixels.clear();
    pixels.show();
    
    // Stop Audio/Tuner
    audio.stopTone();
    tuner.stop();
    
    delay(100);
    u8g2.setPowerSave(1); // Screen off

    // Wakeup Config
    // ESP32 Classic: Use EXT0 for single pin wakeup on LOW
    esp_sleep_enable_ext0_wakeup((gpio_num_t)ENC_BUTTON, 0);

    Serial.println("Entering Deep Sleep...");
    esp_deep_sleep_start();
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
