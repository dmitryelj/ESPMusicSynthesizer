/* A Stylophone-type ESP32 Music Synthesizer v0.1
   Made by Dmitrii <dmitryelj@gmail.com>

   Tools to compile: Ardino IDE

   Hardware:
   - ESP32
   - I2S interface (MAX98357)
   - Some LEDs and resistors
   - Speaker
   - A breadboard or PCB (KiCad schematic + Gerber files are included)
*/

#include <ESP_I2S.h>

// I2S Audio I/O

#define I2S_DIN       13
#define I2S_BCLK      12
#define I2S_LRC       14

// IO controls

#define MODE_LED      23 
#define MODE_BTN      22
#define RANGE_UP_BTN  36
#define VOLUME_INPUT  32
#define INTERNAL_LED  2 

// Piano keys

#define KEY1NP  39    
#define KEY2    19
#define KEY3    21
#define KEY4    18
#define KEY5NP  34

#define KEY6NP  35
#define KEY7    33
#define KEY8    25
#define KEY9    17
#define KEY10   26

#define KEY11   5
#define KEY12   16
#define KEY13   4
#define KEY14   15
#define KEY15   27

// Key states
#define KEY_IDLE     0
#define KEY_PRESSED  1
#define KEY_RELEASED 2

// 1 more octave is available when the "Octave Up" button is pressed
#define KEYS_PHYSICAL 15
#define KEYS_TOTAL    15+12

int   keyPressed[KEYS_PHYSICAL] = {0};
int   keyState[KEYS_TOTAL] = {0};
float keyPhase[KEYS_TOTAL] = {0};
float keyAmplitude[KEYS_TOTAL] = {0};

int octaveUpKeyState = 0;

// Key frequencies
int   keyFrequencies[KEYS_TOTAL] = {
    // Physical keys 
    262,  // C4
    277,  //     C#
    294,  // D4
    311,  //     D#
    330,  // E4
    349,  // F4
    370,  //     F#
    392,  // G4
    415,  //     G#
    440,  // A4
    466,  //     A#
    494,  // B4
    523,  // C5
    554,  //    C#
    587,  // D5
    // Keys with an "octave up" button available
    622,  //    D# 
    659,  // E5
    698,  // F5
    740,  //    F#
    784,  // G5
    831,  //    G#
    880,  // A5
    932,  //    A#
    988,  // B5
    1046, // C6
    1109, //    C#
    1175, // D6
};

// Audio
I2SClass i2s;
volatile int amplitude = 0;     // Amplitude of the output signal
const int SAMPLE_RATE = 11025;  // sample rate in Hz

// Play mode
#define SINE_WAVE      1
#define SQUARE         2
#define SAW_TOOTH      3
#define TRIANGLE       4
volatile int audioMode = SINE_WAVE;


void setup() {
    Serial.begin(115200);
    // Optional: Wait for the port to open (useful for native USB boards)
    while (!Serial) {
      ; // wait
    }

    setupPins();
    setupI2S();

    Serial.println("App Started");
}

bool setupI2S() {
    i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
    // Start I2S at the sample rate with 16-bits per sample
    if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,         
                   I2S_SLOT_MODE_MONO)) {
        return false;
    }
    return true;
}

void setupPins() {
    // LEDs
    pinMode(MODE_LED, OUTPUT);
    digitalWrite(MODE_LED, 0);
    pinMode(INTERNAL_LED, OUTPUT);
    digitalWrite(INTERNAL_LED, 0);
    // Mode key
    pinMode(MODE_BTN, INPUT_PULLUP);
    // 'Range Up' Key
    pinMode(RANGE_UP_BTN, INPUT);
    // Volume
    int potValue = analogRead(VOLUME_INPUT);

    // Play keys
    int allKeys[] = { /*KEY1NP,*/ KEY2, KEY3, KEY4, /*KEY5NP*/
                      /*KEY6NP,*/ KEY7, KEY8, KEY9, KEY10,
                      KEY11, KEY12, KEY13, KEY14, KEY15 };
    for (int pin : allKeys) {
        pinMode(pin, INPUT_PULLUP);
    }
    pinMode(KEY1NP, INPUT);
    pinMode(KEY5NP, INPUT);
    pinMode(KEY6NP, INPUT);
}

void updateKeys() {
    // Update piano keys
    int keys[KEYS_PHYSICAL] = { KEY1NP, KEY2, KEY3, KEY4, KEY5NP,
                                KEY6NP, KEY7, KEY8, KEY9, KEY10,
                                KEY11, KEY12, KEY13, KEY14, KEY15 };

    for (int i = 0; i < KEYS_PHYSICAL; i++) {
        int isPressed = digitalRead(keys[i]) == 0;
        
        // Check if the key is pressed; 1st octave
        if (isPressed == 1 && keyPressed[i] == 0) {
            onKeyDown(i);
            keyPressed[i] = 1;
        }
        // Check if the key is released
        if (isPressed == 0 && keyPressed[i] == 1) {
            onKeyUp(i);
            keyPressed[i] = 0;
        }
    }

    // Update "octave up" key
    int octavePressed = digitalRead(RANGE_UP_BTN) == 0;
    if (octavePressed != octaveUpKeyState) {
        if (octavePressed) 
            onOctavePressDown();
        else 
            onOctavePressUp();
        octaveUpKeyState = octavePressed;
    }
}

void onKeyDown(int keyIndex) {
    // This is NOT an ISR, so Serial.print is safe here 
    Serial.printf("onKeyDown %d\n", keyIndex+1);
    digitalWrite(INTERNAL_LED, 1);

    // Set the audio volume
    updateVolume();

    int isOctaveUpPressed = digitalRead(RANGE_UP_BTN) == 0;
    int index = isOctaveUpPressed ? keyIndex + 12 : keyIndex;
    onVirtualKeyDown(index);
}

void onKeyUp(int keyIndex) {
    // This is NOT an ISR, so Serial.print is safer here 
    Serial.printf("onKeyUp %d\n", keyIndex+1);
    digitalWrite(INTERNAL_LED, 0);

    // An octave up key could also be released
    onVirtualKeyUp(keyIndex);
    onVirtualKeyUp(keyIndex + 12);
}

void onVirtualKeyDown(int keyIndex) {
    if (keyState[keyIndex] == KEY_IDLE) {
        keyPhase[keyIndex] = 0;
    }
    keyState[keyIndex] = KEY_PRESSED;
    keyAmplitude[keyIndex] = 1.0f;
}

void onVirtualKeyUp(int keyIndex) {
    if (keyState[keyIndex] == KEY_PRESSED) {
        keyState[keyIndex] = KEY_RELEASED;
    }
}

void onOctavePressDown() {
    Serial.printf("onOctavePressDown\n");
    // If the key was pressed; generate keyDown event octave higher
    for (int i = 0; i < KEYS_PHYSICAL; i++) {
        if (keyPressed[i] == 1) {
            onVirtualKeyUp(i);
            onVirtualKeyDown(i + 12);
        }
    }
}

void onOctavePressUp() {
    Serial.printf("onOctavePressUp\n");
    // If the key was released; generate keyUp event octave higher
    for (int i = 0; i < KEYS_PHYSICAL; i++) {
        if (keyPressed[i] == 1) {
            onVirtualKeyUp(i + 12);
            onVirtualKeyDown(i);
        }
    }
}

void updateVolume() {
    // ADC range is 0..4096 => 0..16384
    amplitude = 4*analogRead(VOLUME_INPUT);
}

int getNextMode(int mode) {
    if (mode == SINE_WAVE)
        return SQUARE;
    if (mode == SQUARE)
        return SAW_TOOTH;
    if (mode == SAW_TOOTH)
        return TRIANGLE;
    return SINE_WAVE;
}

void updateModeKey() {
    unsigned long currentTime = millis();

    // Read mode pin
    static unsigned long lastPressTime = 0;
    if (digitalRead(MODE_BTN) == 0 && currentTime - lastPressTime > 500) {
        Serial.printf("Output mode is %d\n", audioMode);
        audioMode = getNextMode(audioMode);
        Serial.printf("Output mode set to %d\n", audioMode);
        lastPressTime = currentTime;
    }
}

void generateWaveOutputToneOnly() {
    // Generate a single tone for testing audio
    static float phase = 0;

    int frequency = 440;
    int amplitude = 10000;  // 0..32767
    float value = sin(phase) * amplitude;
    float phaseIncrement = (2.0 * PI * frequency) / SAMPLE_RATE;

    phase += phaseIncrement;
    if (phase >= 2.0 * PI) {
        phase -= 2.0 * PI;
    }

    // Send data to I2S, Mono channel, we send low 8 bits then high 8 bits
    int16_t sample = (int16_t)value;
    i2s.write(sample);
    i2s.write(sample >> 8);
}

void generateWaveOutput() {
    float value = 0;
    for (int i = 0; i < KEYS_TOTAL; i++) {
        if (keyState[i] == KEY_IDLE) continue;

        // Add signal to output
        int frequency = keyFrequencies[i];
        float keyValue = sin(keyPhase[i]);

        // Update the amplitude if the key was released
        if (keyState[i] == KEY_RELEASED) {
            if (keyAmplitude[i] > 0) {
                keyValue *= keyAmplitude[i];
                keyAmplitude[i] -= 0.001;
            } else {
                keyState[i] = KEY_IDLE;
                keyAmplitude[i] = 0;
                keyValue = 0;
            }
        }

        // Add key to output
        value += keyValue * amplitude;

        // Update phase
        float phaseIncrement = (2.0 * PI * frequency) / SAMPLE_RATE;
        keyPhase[i] += phaseIncrement;
        if (keyPhase[i] >= 2.0 * PI) {
            keyPhase[i] -= 2.0 * PI;
        } 
    }

    // Send data to I2S, Mono channel, we send low 8 bits then high 8 bits
    int16_t sample = (int16_t)value;
    i2s.write(sample);
    i2s.write(sample >> 8);
}

void showHeartbeatLED() {
    unsigned long currentTimeMs = millis();
    static unsigned long lastTimeMs = 0;
    if (currentTimeMs - lastTimeMs > 2000) {
        digitalWrite(MODE_LED, 0);
        lastTimeMs = currentTimeMs;
    }
    else if (currentTimeMs - lastTimeMs > 1000) {
        digitalWrite(MODE_LED, 1);
    }
}

void showAudioError() {
    Serial.println("Failed to initialize I2S!");
    while (true) {  
        digitalWrite(INTERNAL_LED, 1);
        digitalWrite(MODE_LED, 1);
        delay(200);
        digitalWrite(INTERNAL_LED, 0);
        digitalWrite(MODE_LED, 0);
        delay(200);
    }
}

void loop() {
    // Read all keys
    updateKeys();

    // Display the heartbeat
    showHeartbeatLED();

    // Generate audio
    generateWaveOutput();  // generateWaveOutputToneOnly();
}
