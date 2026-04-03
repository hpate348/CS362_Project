// ============================================================
// REACTION-TIME ARENA — Player 2 Station (Arduino C)
// ============================================================
// Role: I2C peripheral at address 0x09
//       Monitors pushbutton, measures reaction time,
//       detects false starts, controls LED + buzzer feedback
//
// This code is identical to Player 1 except for the I2C address.
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
const int I2C_ADDRESS = 0x09;  // Player 2 address (ONLY DIFFERENCE)

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
volatile unsigned long goTimestamp     = 0;
volatile int           reactionTime   = 0;
volatile bool          falseStartFlag = false;

// ----- Button Debounce -----
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const int DEBOUNCE_DELAY = 30;

// ----- Feedback Timing -----
unsigned long feedbackStart = 0;
bool feedbackActive = false;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(9600);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Join I2C bus as peripheral
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(receiveCommand);
  Wire.onRequest(sendReactionTime);
  
  Serial.println(F("=== Player 2 Station Ready ==="));
  Serial.print(F("I2C Address: 0x"));
  Serial.println(I2C_ADDRESS, HEX);
  
  blinkLED(3, 100);
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  handleButton();
  handleFeedback();
}

// ============================================================
// BUTTON HANDLING
// ============================================================
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading == LOW && lastButtonState == HIGH) {
      onButtonPress();
    }
  }
  
  lastButtonState = reading;
}

void onButtonPress() {
  switch (playerState) {
    
    case PLAYER_READY:
      // FALSE START
      falseStartFlag = true;
      reactionTime = 9999;
      playerState = PLAYER_FALSE_START;
      
      Serial.println(F("FALSE START! Pressed before GO!"));
      falseStartFeedback();
      break;
      
    case PLAYER_TIMING:
      // Valid press — record reaction time
      reactionTime = (int)(millis() - goTimestamp);
      if (reactionTime < 1) reactionTime = 1;
      if (reactionTime > 9998) reactionTime = 9998;
      
      playerState = PLAYER_DONE;
      
      Serial.print(F("Reaction time: "));
      Serial.print(reactionTime);
      Serial.println(F(" ms"));
      goodPressFeedback();
      break;
      
    case PLAYER_IDLE:
    case PLAYER_DONE:
    case PLAYER_FALSE_START:
      break;
  }
}

// ============================================================
// I2C RECEIVE HANDLER
// ============================================================
void receiveCommand(int numBytes) {
  while (Wire.available()) {
    char command = Wire.read();
    
    switch (command) {
      case 'R':
        playerState = PLAYER_READY;
        reactionTime = 0;
        falseStartFlag = false;
        goTimestamp = 0;
        digitalWrite(LED_PIN, HIGH);
        Serial.println(F(">> READY received. Waiting for GO..."));
        break;
        
      case 'G':
        if (playerState == PLAYER_READY) {
          goTimestamp = millis();
          playerState = PLAYER_TIMING;
          digitalWrite(LED_PIN, LOW);
          Serial.println(F(">> GO received! Timing started."));
        }
        break;
    }
  }
}

// ============================================================
// I2C REQUEST HANDLER
// ============================================================
void sendReactionTime() {
  int timeToSend = 0;
  
  switch (playerState) {
    case PLAYER_DONE:
      timeToSend = reactionTime;
      break;
    case PLAYER_FALSE_START:
      timeToSend = 9999;
      break;
    default:
      timeToSend = 0;
      break;
  }
  
  byte highByte = (timeToSend >> 8) & 0xFF;
  byte lowByte  = timeToSend & 0xFF;
  Wire.write(highByte);
  Wire.write(lowByte);
}

// ============================================================
// FEEDBACK FUNCTIONS
// ============================================================

void goodPressFeedback() {
  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 1000, 150);
  feedbackStart = millis();
  feedbackActive = true;
}

void falseStartFeedback() {
  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 300, 500);
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

