// ============================================================
// REACTION-TIME ARENA — Player 1 Station (Arduino B)
// ============================================================
// Role: I2C peripheral at address 0x08
//       Monitors pushbutton, measures reaction time,
//       detects false starts, controls LED + buzzer feedback
//
// I2C Protocol (Master -> This):
//   'R' = READY (new round, reset state)
//   'G' = GO    (start timing!)
//
// I2C Protocol (This -> Master):
//   Sends 2 bytes: reaction time in ms (high byte, low byte)
//   Special values:
//     0    = not ready / still waiting for button press
//     9999 = false start (pressed before GO)
// ============================================================

#include <Wire.h>

// ----- Configuration -----
const int I2C_ADDRESS = 0x08;  // Player 1 address

// ----- Pin Definitions -----
const int BUTTON_PIN = 7;   // Pushbutton input (active LOW, internal pullup)
const int LED_PIN    = 6;   // LED indicator
const int BUZZER_PIN = 5;   // Piezo buzzer

// ----- Player State Machine -----
enum PlayerState {
  PLAYER_IDLE,        // Waiting for game to start
  PLAYER_READY,       // Received READY, waiting for GO
  PLAYER_TIMING,      // GO received, timing reaction
  PLAYER_DONE,        // Button pressed, time recorded
  PLAYER_FALSE_START  // Pressed before GO
};

volatile PlayerState playerState = PLAYER_IDLE;

// ----- Timing Variables -----
volatile unsigned long goTimestamp     = 0;   // When GO was received
volatile int           reactionTime   = 0;   // Calculated reaction time (ms)
volatile bool          falseStartFlag = false;

// ----- Button Debounce -----
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const int DEBOUNCE_DELAY = 30;  // Shorter debounce for reaction game accuracy

// ----- Feedback Timing -----
unsigned long feedbackStart = 0;
bool feedbackActive = false;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(9600);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Active LOW
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Join I2C bus as peripheral
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(receiveCommand);   // Called when master sends data
  Wire.onRequest(sendReactionTime); // Called when master requests data
  
  Serial.println(F("=== Player 1 Station Ready ==="));
  Serial.print(F("I2C Address: 0x"));
  Serial.println(I2C_ADDRESS, HEX);
  
  // Startup blink
  blinkLED(3, 100);
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  // Handle button presses based on current state
  handleButton();
  
  // Handle timed feedback (LED/buzzer off after duration)
  handleFeedback();
}

// ============================================================
// BUTTON HANDLING (with debounce)
// ============================================================
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // Detect button press (falling edge, active LOW)
    if (reading == LOW && lastButtonState == HIGH) {
      onButtonPress();
    }
  }
  
  lastButtonState = reading;
}

void onButtonPress() {
  switch (playerState) {
    
    case PLAYER_READY:
      // Pressed BEFORE the GO signal -> FALSE START
      falseStartFlag = true;
      reactionTime = 9999;  // Special false start code
      playerState = PLAYER_FALSE_START;
      
      // Feedback: rapid buzzer = bad!
      Serial.println(F("FALSE START! Pressed before GO!"));
      falseStartFeedback();
      break;
      
    case PLAYER_TIMING:
      // Pressed AFTER GO -> record reaction time
      reactionTime = (int)(millis() - goTimestamp);
      
      // Clamp to valid range
      if (reactionTime < 1) reactionTime = 1;
      if (reactionTime > 9998) reactionTime = 9998;  // 9999 reserved
      
      playerState = PLAYER_DONE;
      
      // Feedback: short beep + LED = good!
      Serial.print(F("Reaction time: "));
      Serial.print(reactionTime);
      Serial.println(F(" ms"));
      goodPressFeedback();
      break;
      
    case PLAYER_IDLE:
    case PLAYER_DONE:
    case PLAYER_FALSE_START:
      // Button press ignored in these states
      break;
  }
}

// ============================================================
// I2C RECEIVE HANDLER (called by interrupt)
// ============================================================
void receiveCommand(int numBytes) {
  while (Wire.available()) {
    char command = Wire.read();
    
    switch (command) {
      case 'R':
        // READY: New round, reset everything
        playerState = PLAYER_READY;
        reactionTime = 0;
        falseStartFlag = false;
        goTimestamp = 0;
        
        // Turn on LED to show "ready" state
        digitalWrite(LED_PIN, HIGH);
        
        Serial.println(F(">> READY received. Waiting for GO..."));
        break;
        
      case 'G':
        // GO: Start timing! (only if we haven't false-started)
        if (playerState == PLAYER_READY) {
          goTimestamp = millis();
          playerState = PLAYER_TIMING;
          
          // Flash LED off briefly to signal GO
          digitalWrite(LED_PIN, LOW);
          
          Serial.println(F(">> GO received! Timing started."));
        }
        // If already in FALSE_START state, ignore the GO
        break;
    }
  }
}

// ============================================================
// I2C REQUEST HANDLER (called by interrupt)
// ============================================================
void sendReactionTime() {
  int timeToSend = 0;
  
  switch (playerState) {
    case PLAYER_DONE:
      timeToSend = reactionTime;
      break;
    case PLAYER_FALSE_START:
      timeToSend = 9999;  // False start indicator
      break;
    default:
      timeToSend = 0;  // Not ready yet
      break;
  }
  
  // Send as 2 bytes (high byte first, then low byte)
  byte highByte = (timeToSend >> 8) & 0xFF;
  byte lowByte  = timeToSend & 0xFF;
  Wire.write(highByte);
  Wire.write(lowByte);
}

// ============================================================
// FEEDBACK FUNCTIONS (LED + Buzzer)
// ============================================================

void goodPressFeedback() {
  // Short beep + LED flash for a valid press
  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 1000, 150);  // 1kHz beep for 150ms
  feedbackStart = millis();
  feedbackActive = true;
}

void falseStartFeedback() {
  // Rapid low buzzing + LED flashing for false start
  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 300, 500);  // Low 300Hz buzz for 500ms
  feedbackStart = millis();
  feedbackActive = true;
}

void handleFeedback() {
  if (feedbackActive && (millis() - feedbackStart > 600)) {
    digitalWrite(LED_PIN, LOW);
    noTone(BUZZER_PIN);
    feedbackActive = false;
  }
}

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
}

