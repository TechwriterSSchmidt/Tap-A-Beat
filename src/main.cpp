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
    STATE_AM_SUBDIV,   // Feature 2
    STATE_AM_BPM,      // Adjustment Menu: BPM (via Menu)
    STATE_TAP_TEMPO,   // New Tap State
    STATE_TRAINER_MENU,// Feature 1
    STATE_TIMER_MENU,  // Feature 3
    STATE_PRESETS_MENU, // New Submenu for Presets
    STATE_PRESET_SELECT, // Loading/Saving
    STATE_QUICK_MENU    // New Overlay Menu
};

AppState currentState = STATE_METRONOME;
bool isVolumeFocus = false; // Used for Volume Overlay visibility

// --- Quick Menu State ---
int quickMenuSelection = 0; // 0=Metric, 1=Subdiv, 2=Preset
bool quickMenuEditing = false;

// --- Input Handling State ---
bool isPushAndTurn = false;
int pendingClicks = 0;
unsigned long lastClickReleaseTime = 0;
long encoderValAtPress = 0;
#define DOUBLE_CLICK_GAP 250 // ms

// --- Time Signatures --------------------------------------------------------
struct TimeSig {
    int num;
    int den;
    const char* label;
};

// Common Time Signatures + Jazz/Odd
const TimeSig timeSignatures[] = {
    {1, 4, "1/4"},
    {2, 4, "2/4"},
    {3, 4, "3/4"},
    {4, 4, "4/4"},
    {5, 4, "5/4"},
    {6, 4, "6/4"},
    {7, 4, "7/4"},
    {3, 8, "3/8"},
    {5, 8, "5/8"},
    {6, 8, "6/8"},
    {7, 8, "7/8"},
    {9, 8, "9/8"},
    {12, 8, "12/8"}
};
const int NUM_TIME_SIGS = sizeof(timeSignatures) / sizeof(TimeSig);

// --- Taptronic State --------------------------------------------------------
struct TapEvent {
    unsigned long time;
    float peakLevel;
    bool isAccent;
};
#define MAX_TAP_HISTORY 16
TapEvent tapHistory[MAX_TAP_HISTORY];
int tapHistoryHead = 0;
int tapHistoryCount = 0;

// Peak detection state
bool tapIsPeakFinding = false;
float tapCurrentPeak = 0.0f;
unsigned long tapPeakStartTime = 0;
// Double Threshold Logic
float tapBeatThreshold = 5000000.0f;   // Calculated from sensitivity
float tapAccentThreshold = 8000000.0f; // Calculated from sensitivity * 1.5 approx

// --- Metronome Logic --------------------------------------------------------
struct MetronomeState {
    volatile int bpm = 120;
    volatile bool isPlaying = false;
    unsigned long lastBeatTime = 0;
    volatile int beatCounter = 0; // 0 = first beat (Accent)
    
    // Time Sig State
    int timeSigIdx = 3; // Default 4/4
    
    // Feature 2: Subdivisions
    // 0: None (Quarter Note)
    // 1: Eighths (1/2) -> One click in between
    // 2: Triplets (1/3) -> Two clicks
    // 3: Sixteenths (1/4) -> Three clicks
    int subdivision = 0; 
    const char* subLabels[4] = {"None", "1/8", "1/3", "1/16"};
    
    int getBeatsPerBar() {
        return timeSignatures[timeSigIdx].num;
    }

    void resetBeat() {
        beatCounter = 0;
        lastBeatTime = millis();
    }
} metronome;

// --- Feature 1 & 3 States ---
// Trainer State
bool trainerActive = false;
int trainerStartBPM = 80;
int trainerEndBPM = 120;
int trainerStepBPM = 5;
int trainerBarInterval = 4; // Increase every 4 bars
int trainerBarCounter = 0;
// Trainer Menu UI
int trainerMenuSelection = 0;
bool trainerEditing = false; // Toggle between Nav (false) and Value Edit (true)

// Timer State
bool timerActive = false;
unsigned long timerStartTime = 0;
unsigned long timerDuration = 600000; // 10 minutes default
bool timerAlarmTriggered = false;

TaskHandle_t metronomeTaskHandle = NULL;

// --- Menu Logic -------------------------------------------------------------
// Updated Menu structure for Features
const char* menuItems[] = {"Metric", "Subdiv", "Taptronic", "Trainer", "Timer", "Tuner", "Presets", "Exit"};
int menuSelection = 0;
int menuCount = 8;


// Presets Menu
const char* presetsMenuItems[] = {"Load Preset", "Save Preset", "Back"};
int presetsMenuSelection = 0;
int presetsMenuCount = 3;

enum PresetMode {
    PRESET_LOAD,
    PRESET_SAVE
};
int presetSlot = 0; // 0 to NUM_PRESETS-1
PresetMode presetMode = PRESET_LOAD;
#define NUM_PRESETS 50

// --- Setlist State ---
#define NUM_SETLISTS 5
int setlistID = 0; // 0=None/Default. 1-5 = Specific Setlists.
enum SetlistEditState { SL_NONE, SL_EDITING_ID };
SetlistEditState slState = SL_NONE;
int tempSetlistID = 0;

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
void drawPresetsMenuScreen(); // New
void drawTunerScreen(float freq, String note, int cents);
void drawTimeSigScreen();
void drawBPMScreen(); 
void drawTapScreen();
void drawPresetScreen();
void drawQuickMenuScreen();
void enterDeepSleep();
void saveSettings();
void loadSettings();
void savePreset(int slot);
void loadPreset(int slot);
int getPresetSetlistID(int slot);

// --- Audio Task (High Precision Metronome Trigger on Core 0) ----------------
void metronomeTask(void * parameter) {
    unsigned long lastBeat = millis();
    unsigned long nextSubdivision = 0;
    int subCounter = 0;
    
    for(;;) {
        if (metronome.isPlaying && currentState == STATE_METRONOME) {
            unsigned long interval = 60000 / metronome.bpm;
            unsigned long now = millis();
            
            // Subdivisions Calc
            int subs = metronome.subdivision + 1; // 1, 2, 3, 4 parts
            unsigned long subInterval = interval / subs;

            if (now - lastBeat >= interval) {
                lastBeat = now;
                nextSubdivision = now + subInterval;
                subCounter = 1;
                
                // Play Click
                bool isAccent = (metronome.beatCounter == 0);
                audio.playClick(isAccent, false);
                
                // Advance Beat
                metronome.beatCounter++;
                if (metronome.beatCounter >= metronome.getBeatsPerBar()) {
                    metronome.beatCounter = 0;
                    
                    // Feature 1: Trainer Auto-Increment
                    if (trainerActive) {
                        trainerBarCounter++;
                        if (trainerBarCounter >= trainerBarInterval) {
                            trainerBarCounter = 0;
                            if (metronome.bpm < trainerEndBPM) {
                                metronome.bpm += trainerStepBPM;
                                if (metronome.bpm > trainerEndBPM) metronome.bpm = trainerEndBPM;
                                encoder.setCount(metronome.bpm * 2); // Sync UI
                            }
                        }
                    }
                }
            } else {
                // Check Subdivisions
                if (metronome.subdivision > 0 && subCounter < subs) {
                    if (now >= nextSubdivision) {
                         audio.playClick(false, true); // Play Sub
                         nextSubdivision += subInterval;
                         subCounter++;
                    }
                }
            }
            
            // Feature 3: Timer Check
            if (timerActive && !timerAlarmTriggered) {
                if (now - timerStartTime > timerDuration) {
                    timerAlarmTriggered = true;
                    metronome.isPlaying = false; // Stop metronome
                    // Maybe trigger a long haptic pulse or specific pattern?
                    // For now, rely on UI showing "Time's Up!"
                    hapticEnabled = true; 
                    audio.playClick(true, false); // Single alert
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

// --- Taptronic Logic --------------------------------------------------------
void analyzeTapRhythm() {
    if (tapHistoryCount < 3) return;

    // Reset accents based on current threshold (if dynamic) or just use stored flags
    // finding the indices of accents
    int accentIndices[MAX_TAP_HISTORY];
    int accentCount = 0;
    
    // Scan history backwards to find most recent pattern
    // History is a ring buffer? No, simple array for this session is easier 
    // but the code uses a ring buffer approach "tapHistoryHead".
    // Let's implement correct ring buffer iteration.
    
    // Actually, for simplicity, let's just iterate the valid count up to MAX
    // If we treat it as a linear buffer that resets on silence, it's easier. 
    // (See loop logic: tapHistoryCount resets on timeout).
    // So we can just iterate 0 to tapHistoryCount-1.
    
    for (int i = 0; i < tapHistoryCount; i++) {
        if (tapHistory[i].isAccent) {
            accentIndices[accentCount++] = i;
        }
    }
    
    if (accentCount < 2) return;
    
    // Calculate interval between last two accents
    int lastIdx = accentIndices[accentCount - 1];
    int prevIdx = accentIndices[accentCount - 2];
    int interval = lastIdx - prevIdx; // e.g. Acc at 0, Acc at 4 -> Interval 4 (4/4)
    
    if (interval > 0 && interval <= 12) {
        // Try to match specific time signatures preferred by user
        // Priority: /4 over /8?
        int bestMatch = -1;
        
        // Search in timeSignatures
        for (int i = 0; i < NUM_TIME_SIGS; i++) {
            if (timeSignatures[i].num == interval) {
                bestMatch = i;
                // If we find a x/4, prefer it?
                if (timeSignatures[i].den == 4) break; 
            }
        }
        
        if (bestMatch != -1) {
            metronome.timeSigIdx = bestMatch;
        }
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

    Serial.println("Takt-O-Beat v" APP_VERSION " Ready.");
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
                    
                    // Priority Check: Setlist Editing
                    if (currentState == STATE_PRESET_SELECT && slState == SL_EDITING_ID) {
                        // Confirm Setlist ID
                        setlistID = tempSetlistID; 
                        slState = SL_NONE;
                        // Now save everything
                        savePreset(presetSlot); // uses global setlistID if modified? No, need to pass it or store it.
                        // We need to modify savePreset to use this new value.
                        // Actually, 'setlistID' is a global state logic variable?
                        // Let's ensure savePreset logic uses the tempSetlistID we just confirmed.
                        // We will set a flag or just force the save now.
                        char keySL[16]; sprintf(keySL, "p%d_slist", presetSlot);
                        prefs.putInt(keySL, tempSetlistID);
                        
                        currentState = STATE_MENU; // Done
                         
                    } else if (currentState == STATE_METRONOME) {
                        // Toggle Play/Stop
                        metronome.isPlaying = !metronome.isPlaying;
                        if (metronome.isPlaying) metronome.beatCounter = 0;
                        saveSettings();
                    } else if (currentState == STATE_MENU) {
                        if (menuSelection == 0) { // Metric
                             currentState = STATE_AM_TIME_SIG;
                        } else if (menuSelection == 1) { // Subdiv
                             currentState = STATE_AM_SUBDIV;
                        } else if (menuSelection == 2) { // Tap Tempo
                             currentState = STATE_TAP_TEMPO;
                             tuner.begin(); // Enable mic
                             lastTapTime = 0;
                             tapCount = 0;
                        } else if (menuSelection == 3) { // Trainer (Simple Toggle/Conf for now)
                             // For simplicity: Quick set or navigate to trainer menu
                             // Let's implement basic Trainer Start? Or Menu?
                             // User wants full feature. Let's make a simple config screen later.
                             // For now: Toggle On/Off if already configured, or go to config.
                             currentState = STATE_TRAINER_MENU;
                        } else if (menuSelection == 4) { // Timer
                             currentState = STATE_TIMER_MENU;
                             timerStartTime = now;
                             timerAlarmTriggered = false;
                        } else if (menuSelection == 5) { // Tuner
                             currentState = STATE_TUNER;
                             tuner.begin(); 
                        } else if (menuSelection == 6) { // Presets Menu
                             currentState = STATE_PRESETS_MENU;
                             presetsMenuSelection = 0;
                        } else if (menuSelection == 7) { // Exit
                             currentState = STATE_METRONOME;
                        }
                    } else if (currentState == STATE_PRESETS_MENU) {
                        if (presetsMenuSelection == 0) { // Load
                             currentState = STATE_PRESET_SELECT;
                             presetMode = PRESET_LOAD;
                        } else if (presetsMenuSelection == 1) { // Save
                             currentState = STATE_PRESET_SELECT;
                             presetMode = PRESET_SAVE;
                        } else { // Back
                             currentState = STATE_MENU;
                        }
                    } else if (currentState == STATE_AM_SUBDIV) {
                         currentState = STATE_MENU;
                    } else if (currentState == STATE_TRAINER_MENU) {
                         if (trainerMenuSelection == 4) { // Start/Stop
                             trainerActive = !trainerActive;
                             // Reset counter if starting
                             if (trainerActive) trainerBarCounter = 0;
                         } else {
                             // Toggle Edit Checkbox style
                             trainerEditing = !trainerEditing;
                         }
                    } else if (currentState == STATE_TIMER_MENU) {
                         timerActive = !timerActive;
                         if (timerActive) timerStartTime = millis();
                         currentState = STATE_MENU;
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
                        else {
                             // Regular save (keeps existing setlist ID if we don't change it)
                             // Or should we ask here? User said "Beim Save... Frage nach Setlist Number"
                             // We implemented Hold-to-set. If user just clicks, we save with current Setlist ID = 0 (or previous).
                             // To enforce flow: Maybe onClick triggers Setlist Prompt?
                             // But that slows down quick saves. The "Hold to set" is a good compromise.
                             // Let's stick to: Click = Save immediate. Hold = Configure Setlist then save.
                             savePreset(presetSlot);
                        }
                        currentState = STATE_MENU;
                    }
                } else { // Long Press (> 500ms)
                    if (currentState == STATE_METRONOME) {
                        if (duration > 2000) {
                            metronome.isPlaying = false;
                            currentState = STATE_MENU;
                            isVolumeFocus = false;
                        } else {
                            // Medium Press (0.5 - 2s) -> Toggle Volume Focus
                            isVolumeFocus = !isVolumeFocus;
                            // Reset encoder for smooth transition
                            lastEncoderValue = newEncVal; 
                            encoder.setCount(lastEncoderValue * 2);
                            saveSettings();
                        }
                    } else if (currentState == STATE_PRESET_SELECT && presetMode == PRESET_SAVE) {
                        // Hold on Save Screen -> Enter Setlist Editor
                        slState = SL_EDITING_ID;
                        tempSetlistID = getPresetSetlistID(presetSlot);
                        // Reset encoder to control ID
                        encoder.setCount(tempSetlistID * 2); 
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
                int currentVol = audio.getVolume();
                
                if (delta > 0) {
                    // Turn Right (Increase)
                    if (!hapticEnabled) {
                         hapticEnabled = true; // Re-enable first
                    } else {
                         currentVol += delta * 2;
                         if (currentVol > 100) currentVol = 100;
                         audio.setVolume(currentVol);
                    }
                } else {
                    // Turn Left (Decrease)
                    if (currentVol > 0) {
                         currentVol += delta * 2;
                         if (currentVol < 0) currentVol = 0;
                         audio.setVolume(currentVol);
                    } else {
                         // Already at 0, allow disabling haptic
                         if (hapticEnabled) hapticEnabled = false;
                    }
                }
                saveSettings();
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
        } else if (currentState == STATE_PRESETS_MENU) {
            presetsMenuSelection += delta;
            if (presetsMenuSelection < 0) presetsMenuSelection = 0;
            if (presetsMenuSelection >= presetsMenuCount) presetsMenuSelection = presetsMenuCount - 1;
        } else if (currentState == STATE_AM_TIME_SIG) {
            metronome.timeSigIdx += delta;
            if (metronome.timeSigIdx < 0) metronome.timeSigIdx = 0;
            if (metronome.timeSigIdx >= NUM_TIME_SIGS) metronome.timeSigIdx = NUM_TIME_SIGS - 1; 
        } else if (currentState == STATE_AM_SUBDIV) {
            metronome.subdivision += delta;
            if (metronome.subdivision < 0) metronome.subdivision = 0;
            if (metronome.subdivision > 3) metronome.subdivision = 3;
        } else if (currentState == STATE_TRAINER_MENU) {
            if (!trainerEditing) {
                trainerMenuSelection += delta;
                if (trainerMenuSelection < 0) trainerMenuSelection = 0;
                if (trainerMenuSelection > 4) trainerMenuSelection = 4;
            } else {
                // Editing Values
                if (trainerMenuSelection == 0) { // Start BPM
                    trainerStartBPM += delta;
                    if (trainerStartBPM < 30) trainerStartBPM = 30;
                    if (trainerStartBPM > 300) trainerStartBPM = 300;
                } else if (trainerMenuSelection == 1) { // End BPM
                    trainerEndBPM += delta;
                    if (trainerEndBPM < 30) trainerEndBPM = 30;
                    if (trainerEndBPM > 300) trainerEndBPM = 300;
                } else if (trainerMenuSelection == 2) { // Step
                    trainerStepBPM += delta;
                    if (trainerStepBPM < 1) trainerStepBPM = 1;
                    if (trainerStepBPM > 20) trainerStepBPM = 20;
                } else if (trainerMenuSelection == 3) { // Interval
                    trainerBarInterval += delta;
                    if (trainerBarInterval < 1) trainerBarInterval = 1;
                    if (trainerBarInterval > 100) trainerBarInterval = 100;
                }
            }
        } else if (currentState == STATE_TIMER_MENU) {
             // Adjust minutes
             int mins = timerDuration / 60000;
             mins += delta;
             if (mins < 1) mins = 1;
             if (mins > 60) mins = 60;
             timerDuration = mins * 60000;
        } else if (currentState == STATE_AM_BPM) {
            tempBPM += delta;
            if (tempBPM < 30) tempBPM = 30;
            if (tempBPM > 300) tempBPM = 300;
        } else if (currentState == STATE_PRESET_SELECT) {
            if (slState == SL_EDITING_ID) {
                tempSetlistID += delta;
                if (tempSetlistID < 0) tempSetlistID = 0;
                if (tempSetlistID > NUM_SETLISTS) tempSetlistID = NUM_SETLISTS;
            } else {
                presetSlot += delta;
                if (presetSlot < 0) presetSlot = 0;
                if (presetSlot >= NUM_PRESETS) presetSlot = NUM_PRESETS - 1;
            }
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
        
        // Calculate Thresholds (Dynamic based on Sensitivity)
        tapBeatThreshold = 5000000.0f + (1.0f - tapSensitivity) * 10000000.0f;
        tapAccentThreshold = tapBeatThreshold * 1.5f; // Accent must be 50% louder
        
        if (!tapIsPeakFinding) {
            if (lvl > tapBeatThreshold) {
                 if (now - lastTapTime > 120) { // Debounce
                     tapIsPeakFinding = true;
                     tapCurrentPeak = lvl;
                     tapPeakStartTime = now;
                 }
            }
        } else {
            // 1. Track Peak during window
            if (lvl > tapCurrentPeak) tapCurrentPeak = lvl;
            
            // 2. End of Peak Window (50ms)
            if (now - tapPeakStartTime > 50) {
                tapIsPeakFinding = false;
                lastActivityTime = now;
                
                // Analyze
                bool isAccent = (tapCurrentPeak > tapAccentThreshold);

                // Check Timeout (Reset if pause too long)
                // Use tapPeakStartTime as the event time
                if ((tapPeakStartTime - lastTapTime) > TAP_TIMEOUT) {
                     tapCount = 1;
                     tapIntervalAccumulator = 0;
                     tapHistoryCount = 0;
                } else {
                     tapCount++;
                     unsigned long interval = tapPeakStartTime - lastTapTime;
                     
                     // Filter blips
                     if (tapCount > 1) {
                        tapIntervalAccumulator += interval;
                     
                        // BPM Update
                        if (tapCount >= 2) {
                            float avg = (float)tapIntervalAccumulator / (float)(tapCount - 1);
                            if (avg > 100) { // Prevent div/0 or huge bpm
                                int b = (int)(60000.0f / avg);
                                if (b >= 30 && b <= 300) {
                                    metronome.bpm = b;
                                    encoder.setCount(metronome.bpm * 2);
                                }
                            }
                        }
                     }
                }
                
                // Store in History
                if (tapHistoryCount < MAX_TAP_HISTORY) {
                     TapEvent evt;
                     evt.time = tapPeakStartTime;
                     evt.peakLevel = tapCurrentPeak;
                     evt.isAccent = isAccent;
                     tapHistory[tapHistoryCount] = evt;
                     tapHistoryCount++;
                }

                // Call Pattern Recognition
                analyzeTapRhythm();

                // Update timestamp
                lastTapTime = tapPeakStartTime;
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
        case STATE_PRESETS_MENU:
            drawPresetsMenuScreen();
            break;
        case STATE_AM_TIME_SIG:
            drawTimeSigScreen();
            break;
        case STATE_AM_SUBDIV:
            u8g2.setFont(u8g2_font_profont12_mf);
            u8g2.drawStr(0, 12, "--- SUBDIVISIONS ---");
            u8g2.setFont(u8g2_font_logisoso32_tf);
            {
               int ws = u8g2.getStrWidth(metronome.subLabels[metronome.subdivision]);
               u8g2.setCursor((128-ws)/2, 70);
               u8g2.print(metronome.subLabels[metronome.subdivision]);
            }
            break;
        case STATE_TRAINER_MENU:
            u8g2.setFont(u8g2_font_profont12_mf);
            u8g2.drawStr(0, 12, "-- TRAINER CFG --");
            {
                const char* labels[] = {"Start", "End  ", "Step ", "Bars ", ""};
                for(int i=0; i<5; i++) {
                    int y = 35 + (i * 18);
                    if(trainerMenuSelection == i) u8g2.drawStr(0, y, ">");
                    
                    if(i==4) {
                         u8g2.setCursor(12, y);
                         u8g2.print(trainerActive ? "STOP TRAINER" : "START TRAINER");
                    } else {
                        u8g2.setCursor(12, y);
                        u8g2.print(labels[i]);
                        u8g2.setCursor(60, y);
                        
                        int val = 0;
                        if(i==0) val = trainerStartBPM;
                        else if(i==1) val = trainerEndBPM;
                        else if(i==2) val = trainerStepBPM;
                        else if(i==3) val = trainerBarInterval;
                        
                        if(trainerEditing && trainerMenuSelection == i) {
                            u8g2.print("["); u8g2.print(val); u8g2.print("]");
                        } else {
                            u8g2.print(val);
                        }
                    }
                }
            }
            break;
        case STATE_TIMER_MENU:
            u8g2.setFont(u8g2_font_profont12_mf);
            u8g2.drawStr(0, 12, "-- PRACTICE TIMER --");
            {
                char buf[32];
                sprintf(buf, "Duration: %d min", timerDuration / 60000);
                u8g2.drawStr(10, 50, buf);
                sprintf(buf, "Status: %s", timerActive ? "Running" : "Stopped");
                u8g2.drawStr(10, 70, buf);
            }
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
        case STATE_QUICK_MENU:
            drawQuickMenuScreen();
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
        
        if (audio.getVolume() == 0) {
             u8g2.setFont(u8g2_font_logisoso24_tn); // Keep font size consistent-ish?
             // Show Status
             if (hapticEnabled) {
                 // "VIB" or similar
                 u8g2.setFont(u8g2_font_profont12_mf);
                 u8g2.drawStr(30, 80, "MUTE (VIB)");
             } else {
                 u8g2.setFont(u8g2_font_profont12_mf);
                 u8g2.drawStr(30, 80, "MUTE (LED)");
             }
        } else {
            u8g2.setFont(u8g2_font_logisoso24_tn);
            u8g2.setCursor(45, 85);
            u8g2.print(audio.getVolume());
        }
        
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
        u8g2.print(metronome.getBeatsPerBar());
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
             u8g2.print(timeSignatures[metronome.timeSigIdx].label);
        } else {
             u8g2.print(menuItems[i]);
        }
    }
    u8g2.setDrawColor(1);
}

void drawPresetsMenuScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 10, "- PRESETS -");
    u8g2.drawLine(0, 12, 128, 12);
    
    int startY = 30;
    int h = 18;
    
    for (int i = 0; i < presetsMenuCount; i++) {
        if (i == presetsMenuSelection) {
            u8g2.drawBox(10, startY + i*h - 10, 108, 14);
            u8g2.setDrawColor(0);
        } else {
            u8g2.setDrawColor(1);
        }
        
        int w = u8g2.getStrWidth(presetsMenuItems[i]);
        u8g2.setCursor((128 - w) / 2, startY + i*h);
        u8g2.print(presetsMenuItems[i]);
    }
    u8g2.setDrawColor(1);
}

void drawTimeSigScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 12, "--- TIME SIG ---");
    
    // Big number
    u8g2.setFont(u8g2_font_logisoso42_tn);
    // Center logic approx for "12/8" vs "4/4"
    const char* lbl = timeSignatures[metronome.timeSigIdx].label;
    int w = u8g2.getStrWidth(lbl);
    u8g2.setCursor((128 - w)/2, 70);
    u8g2.print(lbl);
    
    u8g2.setFont(u8g2_font_profont12_mf);
    // Info
    u8g2.setCursor(30, 90);
    int n = metronome.getBeatsPerBar();
    u8g2.print(n);
    if (n == 1) u8g2.print(" Beat/Bar");
    else u8g2.print(" Beats/Bar");

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
    u8g2.drawStr(30, 12, "TAPTRONIC");
    u8g2.drawLine(0, 14, 128, 14);
    
    int cx = 64;
    int cy = 60;
    
    // Heart Outline (Threshold Indicator)
    u8g2.drawLine(cx, cy + 30, cx - 30, cy - 10);
    u8g2.drawLine(cx - 30, cy - 10, cx - 15, cy - 25);
    u8g2.drawLine(cx - 15, cy - 25, cx, cy - 10);
    u8g2.drawLine(cx, cy + 30, cx + 30, cy - 10);
    u8g2.drawLine(cx + 30, cy - 10, cx + 15, cy - 25);
    u8g2.drawLine(cx + 15, cy - 25, cx, cy - 10);
    
    // Level Dependent Filling (VU Meter Style)
    // Scale input level to heart size. 
    // Max scale ~ 2.0 fills the outline.
    float scale = tapInputLevel * 2.5f; 
    if (scale > 2.0f) scale = 2.0f;
    
    if (scale > 0.1f) {
        int r = (int)(8 * scale);
        int dX = (int)(15 * scale);
        int dY_circles = (int)(5 * scale); 
        int dY_tri_top = (int)(1 * scale);
        int dY_tri_bot = (int)(22 * scale);
        int dX_tri = (int)(21 * scale);
        
        u8g2.drawDisc(cx - dX, cy - dY_circles, r);
        u8g2.drawDisc(cx + dX, cy - dY_circles, r);
        u8g2.drawTriangle(cx - dX_tri, cy + dY_tri_top, 
                          cx + dX_tri, cy + dY_tri_top, 
                          cx, cy + dY_tri_bot);
    }
    
    char buf[32];
    sprintf(buf, "Sens: %d%%", (int)(tapSensitivity * 100));
    u8g2.drawStr(5, 120, buf);

    if (tapCount > 1) {
        sprintf(buf, "BPM: %d", metronome.bpm);
        u8g2.drawStr(65, 120, buf);
    } else {
         u8g2.drawStr(65, 120, "TAP NOW!");
    }
    
    // Display Detected Metric and Accent Status
    u8g2.setCursor(95, 30);
    u8g2.print(timeSignatures[metronome.timeSigIdx].label);
    
    if (tapHistoryCount > 0) {
        if (millis() - tapHistory[tapHistoryCount-1].time < 400) {
            u8g2.setCursor(95, 45);
            if (tapHistory[tapHistoryCount-1].isAccent) {
                u8g2.print("ACC!");
            } else {
                u8g2.print("Tap");
            }
        }
    }
}

// --- Helper Functions to read setlist from Prefs for display ---
int getPresetSetlistID(int slot) {
    char keySL[16]; sprintf(keySL, "p%d_slist", slot);
    return prefs.getInt(keySL, 0); // Default 0
}

void drawPresetScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    const char* title = (presetMode == PRESET_LOAD) ? "Load Preset" : "Save Preset";
    u8g2.drawStr(0, 10, title);
    u8g2.drawLine(0, 12, 128, 12);
    
    // Slot Number
    if (slState == SL_EDITING_ID) {
        u8g2.drawStr(20, 35, "Setlist #?");
        u8g2.setFont(u8g2_font_logisoso24_tn);
        char buf[8]; sprintf(buf, "%d", tempSetlistID);
        u8g2.drawStr(60, 70, buf);
        u8g2.setFont(u8g2_font_profont12_mf);
        u8g2.drawStr(30, 100, "Turn: Change");
        u8g2.drawStr(30, 115, "Click: Confirm");
        return;
    }

    char buf[24];
    sprintf(buf, "Slot %d / %d", presetSlot + 1, NUM_PRESETS);
    // Center it roughly
    int w = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - w)/2, 35, buf);
    
    // Preview Info
    char key[16];
    sprintf(key, "p%d_bpm", presetSlot);
    
    // Check if preset exists
    if (!prefs.isKey(key) && presetMode == PRESET_LOAD) {
        // Empty
        u8g2.setFont(u8g2_font_logisoso24_tn); // Or just big text
        u8g2.setFont(u8g2_font_profont12_mf);
        u8g2.drawStr(40, 70, "(Empty)");
    } else {
        // Read values (or what WILL be overwritten)
        int pBpm;
        if (presetMode == PRESET_SAVE && !prefs.isKey(key)) {
             u8g2.drawStr(45, 65, "(New)");
             u8g2.setFont(u8g2_font_profont12_mf);
        } else {
             // Existing data
             pBpm = prefs.getInt(key, 120);
             char keyTs[16]; sprintf(keyTs, "p%d_ts_idx", presetSlot);
             int tsIdx = prefs.getInt(keyTs, 3); // Default 4/4
             if(tsIdx < 0 || tsIdx >= NUM_TIME_SIGS) tsIdx = 3;

             // Display Logic: "4/4 @ 120"
             u8g2.setFont(u8g2_font_logisoso24_tn);
             char infoBuf[16];
             sprintf(infoBuf, "%d", pBpm);
             u8g2.drawStr(10, 80, infoBuf);
             
             u8g2.setFont(u8g2_font_profont12_mf);
             u8g2.drawStr(70, 70, "BPM");
             u8g2.drawStr(70, 85, timeSignatures[tsIdx].label);
             
             // Setlist ID Display
             int sList = getPresetSetlistID(presetSlot);
             if (sList > 0) {
                 char slBuf[20]; sprintf(slBuf, "Set: #%d", sList);
                 u8g2.drawStr(70, 100, slBuf);
             }
        }
    }

    u8g2.setFont(u8g2_font_tiny5_tf);
    if(presetMode == PRESET_SAVE) {
        u8g2.drawStr(10, 115, "Hold Enc: Set Setlist");
    } else {
        u8g2.drawStr(10, 115, "Turn:Select Click:Do");
    }
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
    prefs.putInt("ts_idx", metronome.timeSigIdx); // Changed from ts to ts_idx
    prefs.putInt("vol", audio.getVolume());
    prefs.putFloat("a4", a4Reference);
    prefs.putBool("haptic", hapticEnabled);
}

void loadSettings() {
    metronome.bpm = prefs.getInt("bpm", 120);
    // Migration logic for old ts
    if (prefs.isKey("ts_idx")) {
        metronome.timeSigIdx = prefs.getInt("ts_idx", 3); // 3 = 4/4
    } else {
        // Fallback or migration
        int oldTs = prefs.getInt("ts", 4);
        // Map oldTs (1..12) to indices if valid?
        // E.g. if oldTs == 4, use 4/4 (idx 3)
        // Simple map: if 1..7 use x-1. If >7 default to 4/4
        if (oldTs >=1 && oldTs <= 7) metronome.timeSigIdx = oldTs - 1;
        else metronome.timeSigIdx = 3; 
    }
    
    int vol = prefs.getInt("vol", 50);
    a4Reference = prefs.getFloat("a4", 440.0f);
    hapticEnabled = prefs.getBool("haptic", true);
    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    audio.setVolume(vol);
    tuner.setA4Reference(a4Reference);
}

void savePreset(int slot) {
    char key[16];
    sprintf(key, "p%d_bpm", slot);
    prefs.putInt(key, metronome.bpm);
    sprintf(key, "p%d_ts_idx", slot);
    prefs.putInt(key, metronome.timeSigIdx);
    sprintf(key, "p%d_vol", slot);
    prefs.putInt(key, audio.getVolume());
    sprintf(key, "p%d_a4", slot);
    prefs.putFloat(key, a4Reference);
}

void loadPreset(int slot) {
    char key[16];
    sprintf(key, "p%d_bpm", slot);
    metronome.bpm = prefs.getInt(key, metronome.bpm);
    sprintf(key, "p%d_ts_idx", slot);
    // Presets might need migration too if kept long term, but assuming fresh oroverwrite
    // Default to current sig if fail
    metronome.timeSigIdx = prefs.getInt(key, metronome.timeSigIdx); 
    
    sprintf(key, "p%d_vol", slot);
    int vol = prefs.getInt(key, audio.getVolume());
    sprintf(key, "p%d_a4", slot);
    a4Reference = prefs.getFloat(key, a4Reference);
    tuner.setA4Reference(a4Reference);
    audio.setVolume(vol);
    encoder.setCount(metronome.bpm * 2);
    saveSettings();
}

void drawQuickMenuScreen() {
    // Overlay Style
    u8g2.setDrawColor(0);
    u8g2.drawBox(10, 20, 108, 90);
    u8g2.setDrawColor(1);
    u8g2.drawFrame(10, 20, 108, 90);
    u8g2.drawFrame(12, 22, 104, 86); // Double Frame
    
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawBox(30, 16, 68, 10); // Title BG
    u8g2.setDrawColor(0);
    u8g2.drawStr(36, 24, "QUICK MENU");
    u8g2.setDrawColor(1);
    
    const char* items[] = {"Metric", "Subdiv", "Preset"};
    int yStart = 45;
    
    for(int i=0; i<3; i++) {
        int y = yStart + (i * 20);
        
        if (quickMenuSelection == i) {
            u8g2.drawStr(20, y, ">");
        }
        
        u8g2.drawStr(30, y, items[i]);
        
        // Value Draw
        u8g2.setCursor(75, y);
        if (i == 0) { // Metric
            if (quickMenuEditing && quickMenuSelection == 0) {
                 u8g2.print("["); u8g2.print(timeSignatures[metronome.timeSigIdx].label); u8g2.print("]");
            } else {
                 u8g2.print(timeSignatures[metronome.timeSigIdx].label);
            }
        } else if (i == 1) { // Subdiv
            const char* slLabel = metronome.subLabels[metronome.subdivision];
             if (quickMenuEditing && quickMenuSelection == 1) {
                 u8g2.print("["); u8g2.print(slLabel); u8g2.print("]");
            } else {
                 u8g2.print(slLabel);
            }
        } else if (i == 2) { // Preset
             if (quickMenuEditing && quickMenuSelection == 2) {
                 u8g2.print("[#"); u8g2.print(presetSlot+1); u8g2.print("]");
            } else {
                 u8g2.print("#"); u8g2.print(presetSlot+1);
            }
        }
    }
    
    u8g2.setFont(u8g2_font_tiny5_tf);
    u8g2.drawStr(25, 105, "Click: Edit/Save");
}

