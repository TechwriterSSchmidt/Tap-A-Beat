#include <Arduino.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <Wire.h>
#include "config.h"
#include "AudioEngine.h"
#include "Tuner.h"

// --- Global Objects ---------------------------------------------------------
U8G2_SH1107_128X128_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ I2C_SCL_PIN, /* data=*/ I2C_SDA_PIN);
ESP32Encoder encoder;
AudioEngine audio;
Tuner tuner;

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
const char* menuItems[] = {"Time Signature", "Tuner", "Exit"};
int menuSelection = 0;
int menuCount = 3;

// --- Encoder State ----------------------------------------------------------
long lastEncoderValue = 0;
unsigned long buttonPressTime = 0;
bool buttonActive = false;
bool isVolumeAdjustment = false;
bool isTunerToneOn = false;

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
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
    
    Serial.println("Entering Deep Sleep...");
    esp_deep_sleep_start();
}

// --- Helper Functions -------------------------------------------------------
// ...existing code...
// (Redefine drawTunerScreen)

void drawTunerScreen(float freq, String note, int cents) {
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(0, 10, "--- TUNER ---");
    u8g2.drawLine(0, 12, 128, 12);

    // Mode Status
    if (isTunerToneOn) {
        u8g2.drawStr(80, 10, "[TONE]");
        u8g2.setFont(u8g2_font_profont12_mf);
        u8g2.drawStr(30, 60, "A4 = 440Hz");
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
    // Tuner init happens when entering tuner mode to save I2S buffer
    
    ESP32Encoder::useInternalWeakPullResistors = UP;
    encoder.attachHalfQuad(ENC_PIN_A, ENC_PIN_B);
    encoder.setCount(metronome.bpm * 2);
    pinMode(ENC_BUTTON, INPUT_PULLUP);
    
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

    // Check Button
    bool btnState = (digitalRead(ENC_BUTTON) == LOW);
    
    if (btnState) lastActivityTime = now; // Button held counts as activity

    if (btnState && !buttonActive) {
        // Just pressed
        buttonActive = true;
        buttonPressTime = now;
    } 
    
    // Detect "Press and Turn" for Volume (immediate action)
    if (buttonActive && delta != 0 && currentState == STATE_METRONOME) {
        isVolumeAdjustment = true;
        
        int newVol = audio.getVolume() + delta * 2; // Faster vol change
        if (newVol < 0) newVol = 0; 
        if (newVol > 100) newVol = 100;
        audio.setVolume(newVol);
        
        // Consume the delta so BPM doesn't change
        delta = 0; 
        // Reset encoder match
        encoder.setCount(lastEncoderValue * 2); // Dont move encoder logic pos
        newEncVal = lastEncoderValue;
    }

    if (!btnState && buttonActive) {
        // Just released
        buttonActive = false;
        long duration = now - buttonPressTime;
        isVolumeAdjustment = false;
        
        if (duration < 500) { // Short Click
            if (currentState == STATE_METRONOME) {
                 metronome.isPlaying = !metronome.isPlaying;
                 if (metronome.isPlaying) metronome.resetBeat();
            } else if (currentState == STATE_MENU) {
                // Menu Action
                if (menuSelection == 0) { // Time Sig
                     metronome.beatsPerBar++;
                     if (metronome.beatsPerBar > 7) metronome.beatsPerBar = 2;
                     currentState = STATE_METRONOME; // Quick hack, normally submenu
                } else if (menuSelection == 1) { // Tuner
                     currentState = STATE_TUNER;
                     tuner.begin(); 
                } else if (menuSelection == 2) { // Exit
                     currentState = STATE_METRONOME;
                }
            } else if (currentState == STATE_TUNER) {
                // Toggle Reference Tone
                isTunerToneOn = !isTunerToneOn;
                if (isTunerToneOn) {
                    audio.startTone(440.0); // A4
                    tuner.stop(); // Stop mic
                } else {
                    audio.stopTone();
                    tuner.begin(); // Restart mic
                }
            }
        } else { // Long Press
            if (currentState == STATE_METRONOME) {
                currentState = STATE_MENU;
                metronome.isPlaying = false;
            } else if (currentState == STATE_TUNER) {
                // Exit Tuner
                isTunerToneOn = false;
                audio.stopTone();
                tuner.stop();
                currentState = STATE_MENU;
            }
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
            
        case STATE_TUNER:
            if (isTunerToneOn) {
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
        default:
            break;
    }
    
    u8g2.sendBuffer();

    // --- Auto Off Check -----------------------------------------------------
    // Sleep if inactive for AUTO_OFF_MS AND metronome is NOT playing
    if (!metronome.isPlaying && (now - lastActivityTime > AUTO_OFF_MS)) {
        // Tuner mode is not "playing", so it will sleep after 2 mins of no button/knob use
        // which is good behavior.
        enterDeepSleep();
    }
}

