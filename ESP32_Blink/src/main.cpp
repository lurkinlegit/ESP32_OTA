#include <Arduino.h>

#define LED_BUILTIN 20

// Durations in milliseconds
const unsigned long BLINK_INTERVAL = 200;  // 200ms on, 200ms off (Fast blink)
const unsigned long PAUSE_INTERVAL = 3000; // 3 seconds pause between cycles

// Tracking state variables
unsigned long previousMillis = 0;
int blinkCount = 0;
bool ledState = false;
bool isPausing = false;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  unsigned long currentMillis = millis();

  if (isPausing) {
    // 1. Check if the 3-second long pause is over
    if (currentMillis - previousMillis >= PAUSE_INTERVAL) {
      isPausing = false;         // Pause is over
      blinkCount = 0;            // Reset blink counter for the next cycle
      previousMillis = currentMillis; // Reset clock
    }
  } 
  else {
    // 2. We are actively blinking (Needs 3 full on/off cycles = 6 total state changes)
    if (currentMillis - previousMillis >= BLINK_INTERVAL) {
      previousMillis = currentMillis; // Reset clock
      
      ledState = !ledState;           // Toggle state (LOW to HIGH or HIGH to LOW)
      digitalWrite(LED_BUILTIN, ledState);

      // Every time the LED turns off, we completed 1 full blink
      if (ledState == LOW) {
        blinkCount++;
      }

      // 3. Once we hit 3 blinks, start the long pause
      if (blinkCount >= 3) {
        isPausing = true;
      }
    }
  }
}
