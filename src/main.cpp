#include <Arduino.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <Wire.h>
#include <Preferences.h> // Save settings
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

// --- State Management -------------------------------------------------------
enum AppState {
    STATE_METRONOME,
    STATE_MENU,
    STATE_TUNER,
    STATE_AM_TIME_SIG,
    STATE_AM_BPM // New Edit State
};

AppState currentState = STATE_METRONOME;

// --- Metronome Logic --------------------------------------------------------
struct MetronomeState {
    volatile int bpm = 120;
    volatile bool isPlaying = false;
    volatile int beatCounter = 0; // 0 = first beat (Accent)
    volatile int beatsPerBar = 4; // Top number (4/4 -> 4)
} metronome;

TaskHandle_t audioTaskHandle = NULL;

// --- Menu Logic -------------------------------------------------------------
const char* menuItems[] = {"Speed (BPM)", "Time Signature", "Tuner", "Exit"};
int menuSelection = 0;
int menuCount = 4;

// --- Encoder / Input State --------------------------------------------------
long lastEncoderValue = 0;
unsigned long buttonPressTime = 0;
bool buttonActive = false;
bool isVolumeAdjustment = false;
bool isTunerToneOn = false;

// --- Tap Tempo Logic --------------------------------------------------------
unsigned long lastTapTime = 0;
unsigned long tapIntervalAccumulator = 0;
int tapCount = 0;
bool showTapVisual = false;
unsigned long tapVisualStartTime = 0;
#define TAP_TIMEOUT 2000 // Reset tap sequence after 2s silence

// --- Power Management -------------------------------------------------------
unsigned long lastActivityTime = 0;

// --- Forward Declarations ---------------------------------------------------
void drawMetronomeScreen();
void drawMenuScreen();
void drawTunerScreen(float freq, String note, int cents);
void drawTimeSigScreen();
void drawBPMScreen(); // New
void enterDeepSleep();

// --- Audio Task (High Precision on Core 0) ----------------------------------
void audioTask(void * parameter) {
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
        
        // Tuner Tone logic here to unblock UI
        if (currentState == STATE_TUNER && isTunerToneOn) {
            audio.updateTone();
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
    
    // Preferences Init
    prefs.begin("taktobeat", false);
    metronome.bpm = prefs.getInt("bpm", 120);
    metronome.beatsPerBar = prefs.getInt("sig", 4);
    int savedVol = prefs.getInt("vol", 50);
    audio.setVolume(savedVol);
    
    ESP32Encoder::useInternalWeakPullResistors = UP;
    encoder.attachHalfQuad(ENC_PIN_A, ENC_PIN_B);
    encoder.setCount(metronome.bpm * 2);
    pinMode(ENC_BUTTON, INPUT_PULLUP);
    
    // Start Mic for Tap Tempo if desired?
    // We only enable mic when not playing to save power/conflict?
    // Actually, users might want to tap *while* playing to sync? 
    // Hard to distinguish click from tap. Let's enable when STOPPED.
    tuner.begin(); 
    
    lastActivityTime = millis();
    
    // Create Audio Task on Core 0
    xTaskCreatePinnedToCore(
      audioTask,   "AudioTask", 
      4096,        NULL, 
      2,           &audioTaskHandle, 
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
    bool btnState = (digitalRead(ENC_BUTTON) == LOW);
    if (btnState) lastActivityTime = now;

    if (btnState && !buttonActive) {
        buttonActive = true;
        buttonPressTime = now;
    } 
    
    // Volume Control (Press & Turn)
    if (buttonActive && delta != 0 && currentState == STATE_METRONOME) {
        isVolumeAdjustment = true;
        
        int newVol = audio.getVolume() + delta * 2;
        if (newVol < 0) newVol = 0; 
        if (newVol > 100) newVol = 100;
        audio.setVolume(newVol);
        
        delta = 0; 
        encoder.setCount(lastEncoderValue * 2); 
        newEncVal = lastEncoderValue;
    }

    if (!btnState && buttonActive) { // Released
        buttonActive = false;
        long duration = now - buttonPressTime;
        isVolumeAdjustment = false;
        
        if (duration < 500) { // Short Click
            if (currentState == STATE_METRONOME) {
                 metronome.isPlaying = !metronome.isPlaying;
                 if (metronome.isPlaying) metronome.beatCounter = 0;
            } else if (currentState == STATE_MENU) {
                if (menuSelection == 0) { // BPM
                     currentState = STATE_AM_BPM;
                } else if (menuSelection == 1) { // Time Sig
                     currentState = STATE_AM_TIME_SIG;
                } else if (menuSelection == 2) { // Tuner
                     currentState = STATE_TUNER;
                     tuner.begin(); 
                } else if (menuSelection == 3) { // Exit
                     currentState = STATE_METRONOME;
                }
            } else if (currentState == STATE_TUNER) {
                isTunerToneOn = !isTunerToneOn;
                if (isTunerToneOn) {
                    audio.startTone(440.0);
                    tuner.stop();
                } else {
                    audio.stopTone();
                    tuner.begin();
                }
            } else if (currentState == STATE_AM_TIME_SIG) {
                // Confirm selection -> Go back to Menu (or Metronome?)
                currentState = STATE_MENU;
            } else if (currentState == STATE_AM_BPM) {
                // Confirm selection -> Go back to Menu
                currentState = STATE_MENU;
            }
        } else { // Long Press
            if (currentState == STATE_METRONOME) {
                metronome.isPlaying = false;
                currentState = STATE_MENU;
            } else if (currentState == STATE_TUNER) {
                isTunerToneOn = false;
                audio.stopTone();
                tuner.stop();
                currentState = STATE_MENU;
            } else if (currentState == STATE_MENU) {
                currentState = STATE_METRONOME;
            } else if (currentState == STATE_AM_TIME_SIG) {
                currentState = STATE_MENU; // Cancel / Back
            } else if (currentState == STATE_AM_BPM) {
                currentState = STATE_MENU; // Cancel / Back
            }
        }
    }

    // Encoder State Logic
    if (delta != 0 && !isVolumeAdjustment) {
        if (currentState == STATE_METRONOME) {
            metronome.bpm += delta;
            if (metronome.bpm < 30) metronome.bpm = 30;
            if (metronome.bpm > 300) metronome.bpm = 300;
        } else if (currentState == STATE_MENU) {
            menuSelection += delta;
            if (menuSelection < 0) menuSelection = 0;
            if (menuSelection >= menuCount) menuSelection = menuCount - 1;
        } else if (currentState == STATE_AM_TIME_SIG) {
            metronome.beatsPerBar += delta;
            if (metronome.beatsPerBar < 1) metronome.beatsPerBar = 1;
            if (metronome.beatsPerBar > 12) metronome.beatsPerBar = 12; // Allow up to 12
        } else if (currentState == STATE_AM_BPM) {
            metronome.bpm += delta;
            if (metronome.bpm < 30) metronome.bpm = 30;
            if (metronome.bpm > 300) metronome.bpm = 300;
        }
        lastEncoderValue = newEncVal;
    }

    // --- Tap Tempo Detection (Only if Metronome stopped) ---
    if (currentState == STATE_METRONOME && !metronome.isPlaying) {
         int32_t amp = tuner.getAmplitude();
         // Serial.println(amp); // debug
         
         // Threshold check (tune this!)
         if (amp > 8000000) { // Needs empirical testing
             if (now - lastTapTime > 150) { // Debounce 150ms
                 
                 lastActivityTime = now;
                 showTapVisual = true;
                 tapVisualStartTime = now;
                 
                 // Logic
                 if (now - lastTapTime > TAP_TIMEOUT) {
                     // First tap of new sequence
                     tapCount = 1;
                     tapIntervalAccumulator = 0;
                 } else {
                     tapCount++;
                     tapIntervalAccumulator += (now - lastTapTime);
                     
                     if (tapCount >= 2) {
                         // Calculate BPM
                         float avgInterval = (float)tapIntervalAccumulator / (float)(tapCount - 1);
                         int newBpm = (int)(60000.0 / avgInterval);
                         
                         if (newBpm >= 30 && newBpm <= 300) {
                             metronome.bpm = newBpm;
                             encoder.setCount(metronome.bpm * 2); // Update encoder tracking
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
        case STATE_TUNER:
             if (isTunerToneOn) {
                 drawTunerScreen(440, "A4", 0);
             } else {
                 float f = tuner.getFrequency();
                 int cents = 0;
                 String n = tuner.getNote(f, cents);
                 drawTunerScreen(f, n, cents); 
             }
            break;
    }
    
    u8g2.sendBuffer();

    // 3. Auto Off
    if (!metronome.isPlaying && currentState != STATE_TUNER && (now - lastActivityTime > AUTO_OFF_MS)) {
        enterDeepSleep();
    }
}

// --- Drawing Implementation -------------------------------------------------

void drawMetronomeScreen() {
    // BPM
    u8g2.setFont(u8g2_font_logisoso42_tn);
    u8g2.setCursor(20, 60);
    u8g2.print(metronome.bpm);
    
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(95, 60, "BPM");

    // Beat Visual
    int cx = 64; 
    int cy = 90;
    
    // Tap Visual
    if (showTapVisual) {
        if (millis() - tapVisualStartTime > 200) showTapVisual = false;
        u8g2.setFont(u8g2_font_logisoso24_tn);
        u8g2.drawStr(35, 110, "TAP!");
        return; // Override standard visual
    }

    // Volume Overlay (when adjusting)
    if (buttonActive) {
        u8g2.setDrawColor(0);
        u8g2.drawBox(14, 40, 100, 50); // Clear area
        u8g2.setDrawColor(1);
        u8g2.drawFrame(14, 40, 100, 50);
        
        u8g2.setFont(u8g2_font_profont12_mf);
        u8g2.drawStr(20, 55, "VOLUME");
        
        u8g2.setFont(u8g2_font_logisoso24_tn);
        u8g2.setCursor(45, 85);
        u8g2.print(audio.getVolume());
        return; // Skip drawing the rest of the standard UI
    }

    if (metronome.isPlaying) {
        u8g2.drawDisc(cx, cy, 10 + (metronome.beatCounter % 2)*4); // Pulse
        
        u8g2.setCursor(45, 115);
        u8g2.print(metronome.beatCounter + 1);
        u8g2.print("/");
        u8g2.print(metronome.beatsPerBar);
    } else {
        u8g2.drawCircle(cx, cy, 10);
        u8g2.setCursor(48, 115);
        u8g2.print("STOP");
    }
    
    // Volume
    int volW = map(audio.getVolume(), 0, 100, 0, 128);
    u8g2.drawBox(0, 124, volW, 4);
}

void drawMenuScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 12, "--- MENU ---");
    
    int startY = 30;
    int h = 16;
    
    for (int i = 0; i < menuCount; i++) {
        if (i == menuSelection) {
            u8g2.drawStr(0, startY + i*h + 10, ">");
        }
        u8g2.setCursor(12, startY + i*h + 10);
        
        if (i == 0) {
             // Dynamic Label for BPM
             u8g2.print("Speed: ");
             u8g2.print(metronome.bpm);
             u8g2.print(" bpm");
        } else if (i == 1) {
             // Dynamic Label for Time Sig
             u8g2.print("Metric: ");
             u8g2.print(metronome.beatsPerBar);
             u8g2.print("/4");
        } else {
             u8g2.print(menuItems[i]);
        }
    }
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
        u8g2.drawStr(30, 60, "A4 = 440Hz");
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

    if (abs(cents) < 5) u8g2.drawStr(50, 90, "* OK *");
    else if (cents < 0) u8g2.drawStr(10, 90, "<< FLAT");
    else u8g2.drawStr(80, 90, "SHARP >>");
}

void drawBPMScreen() {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 12, "--- SET SPEED ---");
    
    // Big number
    u8g2.setFont(u8g2_font_logisoso42_tn);
    int w = u8g2.getStrWidth(String(metronome.bpm).c_str());
    u8g2.setCursor((128 - w) / 2, 70); 
    u8g2.print(metronome.bpm);
    
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(54, 90, "BPM");
    
    // Arrows
    u8g2.drawTriangle(10, 50, 25, 40, 25, 60); // Left
    u8g2.drawTriangle(118, 50, 103, 40, 103, 60); // Right
}

void enterDeepSleep() {
    // Save Settings
    prefs.putInt("bpm", metronome.bpm);
    prefs.putInt("sig", metronome.beatsPerBar);
    prefs.putInt("vol", audio.getVolume());
    prefs.end();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(30, 64, "Infos Saved.");
    u8g2.drawStr(35, 80, "Good Bye!");
    u8g2.sendBuffer();
    delay(800);
    
    u8g2.setPowerSave(1); // Screen off
    
    // Stop Audio
    audio.stopTone(); 
    // Tuner stop
    tuner.stop();

    // Config Wakeup on Button (Pin 6 = ENC_BUTTON) Low
    // ESP32-S3 Ext1 Wakeup
    // Mask for GPIO 6: 1 << 6
    uint64_t mask = (1ULL << ENC_BUTTON);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
    
    Serial.println("Entering Deep Sleep...");
    esp_deep_sleep_start();
}
