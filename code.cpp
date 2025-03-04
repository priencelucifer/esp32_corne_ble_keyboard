#include <BleKeyboard.h>
#include <esp_sleep.h>  // For deep sleep functionality

// Create the BLE Keyboard instance
BleKeyboard bleKeyboard("ESP32 BLE Keyboard", "MyCompany", 100);

// Total number of keys (21 keys)
const int numKeys = 21;

// Define the GPIO pins for each key in the specified order:
// D13, D12, D14, D27, D26, D25, D33, D32, D15, D2, RX2, TX2, D5, D18, D19, D21, RX0, TX0, D22, D23, D4
int keyPins[numKeys] = {13, 12, 14, 27, 26, 25, 33, 32, 15, 2, 16, 17, 5, 18, 19, 21, 3, 1, 22, 23, 4};

// Map each key to a character: 'a' through 'u'
char keyChars[numKeys] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
                          'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u'};

// Define activation logic for each key:
// All keys are active-low (INPUT_PULLUP) except D2 (index 9) which is active-high.
bool activeHigh[numKeys] = {
  false, false, false, false, false, false, false, false, false, true,
  false, false, false, false, false, false, false, false, false, false, false
};

// Track whether a key is pressed (for auto-repeat)
bool buttonState[numKeys] = {false};
// Store the last time a key event was sent (for auto-repeat timing)
unsigned long lastSent[numKeys] = {0};

// Timing constants (in milliseconds)
const unsigned long initialAutoRepeatDelay = 300;     // first auto-repeat delay: 300 ms
const unsigned long subsequentAutoRepeatDelay = 50;     // subsequent auto-repeat delay: 50 ms

// Per-key flag to know if the first auto-repeat has occurred
bool firstAutoRepeat[numKeys] = {false};

// Variable to track when BLE became disconnected
unsigned long notConnectedStartTime = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Keyboard...");

  // Initialize each key pin.
  // For keys other than D2 (index 9), use the appropriate internal resistor:
  // - Active-low keys: INPUT_PULLUP.
  // - Active-high keys: INPUT_PULLDOWN.
  // D2 will be reconfigured dynamically based on BLE connection status.
  for (int i = 0; i < numKeys; i++) {
    if (i == 9) { // D2
      pinMode(keyPins[i], INPUT_PULLDOWN);
    } else {
      if (activeHigh[i])
        pinMode(keyPins[i], INPUT_PULLDOWN);
      else
        pinMode(keyPins[i], INPUT_PULLUP);
    }
    buttonState[i] = false;
    lastSent[i] = 0;
    firstAutoRepeat[i] = false;
  }

  bleKeyboard.begin();
}

void loop() {
  bool connected = bleKeyboard.isConnected();
  unsigned long now = millis();

  // Check BLE connection status and enter deep sleep if not connected for more than 10 seconds.
  if (!connected) {
    if (notConnectedStartTime == 0) {
      notConnectedStartTime = now;
    } else if (now - notConnectedStartTime > 10000) { // 10 seconds timeout
      Serial.println("BLE not connected for 10 seconds. Entering deep sleep...");
      // Configure wake-up on D4 (GPIO 4) for low level (button pressed / GND)
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);
      esp_deep_sleep_start();
    }
  } else {
    notConnectedStartTime = 0;
  }

  // Flag to indicate if any key was newly pressed in this loop iteration.
  bool newKeyPressed = false;

  // Process each key
  for (int i = 0; i < numKeys; i++) {
    // Special handling for D2 (index 9)
    if (i == 9) {
      if (!connected) {
        // When not connected, set D2 as OUTPUT and blink as an LED.
        pinMode(keyPins[i], OUTPUT);
        // Blink: Toggle every 250 ms (250ms ON, 250ms OFF)
        digitalWrite(keyPins[i], ((now / 250) % 2 == 0) ? HIGH : LOW);
        continue;  // Skip key processing for D2 while blinking.
      } else {
        // When connected, reconfigure D2 as input (active-high) for key reading.
        pinMode(keyPins[i], INPUT_PULLDOWN);
      }
    }

    // Read the key state based on its activation logic.
    bool pressed = activeHigh[i] ? (digitalRead(keyPins[i]) == HIGH)
                                 : (digitalRead(keyPins[i]) == LOW);

    if (pressed) {
      if (!buttonState[i]) {
        // New key press: register immediately.
        buttonState[i] = true;
        firstAutoRepeat[i] = true;  // Mark that the first auto-repeat delay is pending.
        lastSent[i] = now;
        newKeyPressed = true;
        if (connected) {
          Serial.print("Key ");
          Serial.print(keyChars[i]);
          Serial.println(" pressed");
          bleKeyboard.write(keyChars[i]);
        }
      } else {
        // Key is held down.
        if (firstAutoRepeat[i]) {
          // Wait for the initial auto-repeat delay (300 ms).
          if (now - lastSent[i] >= initialAutoRepeatDelay) {
            lastSent[i] = now;
            firstAutoRepeat[i] = false;  // First auto-repeat occurred.
            if (connected) {
              Serial.print("Key ");
              Serial.print(keyChars[i]);
              Serial.println(" auto-repeat (first)");
              bleKeyboard.write(keyChars[i]);
            }
          }
        } else {
          // Subsequent auto-repeat events every 50 ms.
          if (now - lastSent[i] >= subsequentAutoRepeatDelay) {
            lastSent[i] = now;
            if (connected) {
              Serial.print("Key ");
              Serial.print(keyChars[i]);
              Serial.println(" auto-repeat");
              bleKeyboard.write(keyChars[i]);
            }
          }
        }
      }
    } else {
      // Reset state when key is released.
      buttonState[i] = false;
    }
  }
  
  // Debouncing delay:
  // If a new key press was detected this loop, wait 300 ms (to ensure proper debounce before auto-repeat starts);
  // otherwise, use a 50 ms delay.
  if (newKeyPressed)
    delay(300);
  else
    delay(50);
}
