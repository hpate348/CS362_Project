// ============================================================
// REACTION-TIME ARENA — Master Scoreboard Controller (Arduino A)
// ============================================================
// Role: I2C master, game state machine, LCD display, start/reset button
// Communicates with Player 1 (address 0x08) and Player 2 (address 0x09)
//
// I2C Protocol (Master -> Player):
//   'R' = READY (new round starting)
//   'G' = GO    (start timing now!)
//
// I2C Protocol (Player -> Master):
//   Sends 2 bytes: high byte and low byte of reaction time in ms
//   Special value 9999 = false start detected
// ============================================================

#include <Wire.h>
#include <LiquidCrystal.h>

// ----- LCD Pin Wiring (parallel mode) -----
// RS=12, EN=11, D4=5, D5=4, D6=3, D7=2
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// ----- Pin Definitions -----
const int START_BUTTON_PIN = 8;   // Start/Reset pushbutton
const int STATUS_LED_PIN   = 13;  // Optional status LED (onboard)

// ----- I2C Addresses -----
const int PLAYER1_ADDR = 0x08;
const int PLAYER2_ADDR = 0x09;

// ----- Game Configuration -----
const int   TOTAL_ROUNDS       = 5;
const int   FALSE_START_PENALTY = 500;  // ms added for false start
const long  MIN_DELAY          = 2000;  // minimum random delay (ms)
const long  MAX_DELAY          = 5000;  // maximum random delay (ms)
const long  RESPONSE_TIMEOUT   = 5000;  // max time to wait for press (ms)

// ----- Game State Machine -----
enum GameState {
  STATE_WELCOME,      // Showing welcome screen
  STATE_WAIT_START,   // Waiting for start button
  STATE_COUNTDOWN,    // "Get Ready..." phase
  STATE_RANDOM_DELAY, // Random delay before GO
  STATE_GO,           // GO signal sent, waiting for responses
  STATE_SHOW_RESULT,  // Showing round result
  STATE_SHOW_STATS,   // Showing end-of-game statistics
  STATE_GAME_OVER     // Final screen, wait for restart
};

GameState gameState = STATE_WELCOME;

// ----- Score Tracking -----
int   currentRound = 0;
int   p1Times[5];          // Player 1 times per round (ms)
int   p2Times[5];          // Player 2 times per round (ms)
bool  p1FalseStart[5];     // Track false starts
bool  p2FalseStart[5];
int   p1Wins = 0;
int   p2Wins = 0;

// ----- Timing Variables -----
unsigned long stateTimer    = 0;
unsigned long randomDelay   = 0;
unsigned long goSentTime    = 0;
bool p1Responded = false;
bool p2Responded = false;
int  p1ReactionTime = 0;
int  p2ReactionTime = 0;

// ----- Button Debounce -----
bool lastButtonState  = LOW;
bool buttonPressed    = false;
unsigned long lastDebounceTime = 0;
const int DEBOUNCE_DELAY = 50;

// ============================================================
// SETUP
void setup() {
  Wire.begin();
  Serial.begin(9600);
  
  lcd.begin(16, 2);
  
  pinMode(START_BUTTON_PIN, INPUT);  // external pull-down resistor
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  randomSeed(analogRead(A0));
  
  showWelcome();
  
  Serial.println(F("=== Reaction-Time Arena Master ==="));
  Serial.println(F("Waiting for start button..."));
}

void loop() {
  readButton();
  
  switch (gameState) {
    case STATE_WELCOME:
      handleWelcome();
      break;
    case STATE_WAIT_START:
      handleWaitStart();
      break;
    case STATE_COUNTDOWN:
      handleCountdown();
      break;
    case STATE_RANDOM_DELAY:
      handleRandomDelay();
      break;
    case STATE_GO:
      handleGo();
      break;
    case STATE_SHOW_RESULT:
      handleShowResult();
      break;
    case STATE_SHOW_STATS:
      handleShowStats();
      break;
    case STATE_GAME_OVER:
      handleGameOver();
      break;
  }
}

// ============================================================
// BUTTON READING (with debounce)
// ============================================================
void readButton() {
  buttonPressed = false;
  
  if (digitalRead(START_BUTTON_PIN) == HIGH) {
    delay(200);
    buttonPressed = true;
  }
}
// ============================================================
// STATE HANDLERS
// ============================================================

void handleWelcome() {
  // Welcome screen shown for 3 seconds, then move to wait
  if (millis() - stateTimer > 3000) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Press START to");
    lcd.setCursor(0, 1);
    lcd.print("begin the game!");
    gameState = STATE_WAIT_START;
  }
}

void handleWaitStart() {
  if (buttonPressed) {
    Serial.println(F("Start button pressed! Beginning game..."));
    resetGame();
    startNewRound();
  }
}

void handleCountdown() {
  // Show "Get Ready..." for 2 seconds
  if (millis() - stateTimer > 2000) {
    // Transition to random delay
    randomDelay = random(MIN_DELAY, MAX_DELAY + 1);
    stateTimer = millis();
    gameState = STATE_RANDOM_DELAY;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("  Wait for it..");
    lcd.setCursor(0, 1);
    lcd.print("   Don't press!");
    
    Serial.print(F("Random delay: "));
    Serial.print(randomDelay);
    Serial.println(F(" ms"));
  }
}

void handleRandomDelay() {
  if (millis() - stateTimer >= randomDelay) {
    // Send GO signal to both players
    sendCommandToPlayer(PLAYER1_ADDR, 'G');
    sendCommandToPlayer(PLAYER2_ADDR, 'G');
    goSentTime = millis();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("  >>> GO!! <<<  ");
    lcd.setCursor(0, 1);
    lcd.print("  PRESS NOW!!   ");
    
    digitalWrite(STATUS_LED_PIN, HIGH);
    
    p1Responded = false;
    p2Responded = false;
    p1ReactionTime = 0;
    p2ReactionTime = 0;
    
    gameState = STATE_GO;
    
    Serial.println(F("GO signal sent!"));
  }
}

void handleGo() {
  // Poll both players for their reaction times
  if (!p1Responded) {
    int result = requestReactionTime(PLAYER1_ADDR);
    if (result > 0) {
      p1Responded = true;
      p1ReactionTime = result;
      Serial.print(F("P1 raw time: "));
      Serial.println(result);
    }
  }
  
  if (!p2Responded) {
    int result = requestReactionTime(PLAYER2_ADDR);
    if (result > 0) {
      p2Responded = true;
      p2ReactionTime = result;
      Serial.print(F("P2 raw time: "));
      Serial.println(result);
    }
  }
  
  // Check for timeout
  if (millis() - goSentTime > RESPONSE_TIMEOUT) {
    if (!p1Responded) { p1Responded = true; p1ReactionTime = RESPONSE_TIMEOUT; }
    if (!p2Responded) { p2Responded = true; p2ReactionTime = RESPONSE_TIMEOUT; }
  }
  
  // Both responded
  if (p1Responded && p2Responded) {
    digitalWrite(STATUS_LED_PIN, LOW);
    processRoundResult();
  }
}

void handleShowResult() {
  // Show result for 4 seconds, then next round or stats
  if (millis() - stateTimer > 4000) {
    if (currentRound >= TOTAL_ROUNDS) {
      showFinalStats();
    } else {
      startNewRound();
    }
  }
}

void handleShowStats() {
  // Cycle through stats pages on button press
  if (buttonPressed) {
    showGameOver();
  }
  // Auto-advance after 8 seconds
  if (millis() - stateTimer > 8000) {
    showGameOver();
  }
}

void handleGameOver() {
  if (buttonPressed) {
    showWelcome();
  }
}

// ============================================================
// GAME LOGIC
// ============================================================

void resetGame() {
  currentRound = 0;
  p1Wins = 0;
  p2Wins = 0;
  for (int i = 0; i < TOTAL_ROUNDS; i++) {
    p1Times[i] = 0;
    p2Times[i] = 0;
    p1FalseStart[i] = false;
    p2FalseStart[i] = false;
  }
}

void startNewRound() {
  currentRound++;
  
  // Send READY signal to both players
  sendCommandToPlayer(PLAYER1_ADDR, 'R');
  sendCommandToPlayer(PLAYER2_ADDR, 'R');
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("== Round ");
  lcd.print(currentRound);
  lcd.print("/");
  lcd.print(TOTAL_ROUNDS);
  lcd.print(" ==");
  lcd.setCursor(0, 1);
  lcd.print("  Get Ready...  ");
  
  stateTimer = millis();
  gameState = STATE_COUNTDOWN;
  
  Serial.print(F("\n--- Round "));
  Serial.print(currentRound);
  Serial.println(F(" ---"));
}

void processRoundResult() {
  int idx = currentRound - 1;
  
  // Check for false starts (signaled by special value 9999)
  p1FalseStart[idx] = (p1ReactionTime == 9999);
  p2FalseStart[idx] = (p2ReactionTime == 9999);
  
  // Apply false start penalties
  if (p1FalseStart[idx]) {
    // Request the actual early-press time, but apply penalty
    // Player sends 9999 to indicate false start
    // We record it as their reaction time + penalty
    p1Times[idx] = FALSE_START_PENALTY + 500; // penalty time
    Serial.println(F("P1: FALSE START! +500ms penalty"));
  } else {
    p1Times[idx] = p1ReactionTime;
  }
  
  if (p2FalseStart[idx]) {
    p2Times[idx] = FALSE_START_PENALTY + 500;
    Serial.println(F("P2: FALSE START! +500ms penalty"));
  } else {
    p2Times[idx] = p2ReactionTime;
  }
  
  // Determine round winner (lower time wins)
  if (p1Times[idx] < p2Times[idx]) {
    p1Wins++;
  } else if (p2Times[idx] < p1Times[idx]) {
    p2Wins++;
  }
  // Ties: neither gets a win point
  
  // Display round result on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  
  // Row 0: P1 result
  lcd.print("P1:");
  if (p1FalseStart[idx]) {
    lcd.print("EARLY!");
  } else {
    lcd.print(p1Times[idx]);
    lcd.print("ms");
  }
  
  // Row 1: P2 result
  lcd.setCursor(0, 1);
  lcd.print("P2:");
  if (p2FalseStart[idx]) {
    lcd.print("EARLY!");
  } else {
    lcd.print(p2Times[idx]);
    lcd.print("ms");
  }
  
  // Show winner indicator on right side
  lcd.setCursor(12, 0);
  if (p1Times[idx] <= p2Times[idx]) {
    lcd.print("<-W");
  }
  lcd.setCursor(12, 1);
  if (p2Times[idx] < p1Times[idx]) {
    lcd.print("<-W");
  }
  
  // Serial logging
  Serial.print(F("Round "));
  Serial.print(currentRound);
  Serial.print(F(" | P1: "));
  Serial.print(p1Times[idx]);
  Serial.print(F("ms | P2: "));
  Serial.print(p2Times[idx]);
  Serial.print(F("ms | Score: P1="));
  Serial.print(p1Wins);
  Serial.print(F(" P2="));
  Serial.println(p2Wins);
  
  stateTimer = millis();
  gameState = STATE_SHOW_RESULT;
}

void showFinalStats() {
  // Calculate averages and best times
  long p1Total = 0, p2Total = 0;
  int  p1Best = 9999, p2Best = 9999;
  
  for (int i = 0; i < TOTAL_ROUNDS; i++) {
    p1Total += p1Times[i];
    p2Total += p2Times[i];
    if (p1Times[i] < p1Best) p1Best = p1Times[i];
    if (p2Times[i] < p2Best) p2Best = p2Times[i];
  }
  
  int p1Avg = p1Total / TOTAL_ROUNDS;
  int p2Avg = p2Total / TOTAL_ROUNDS;
  
  // Show averages
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AVG P1:");
  lcd.print(p1Avg);
  lcd.print("ms");
  lcd.setCursor(0, 1);
  lcd.print("AVG P2:");
  lcd.print(p2Avg);
  lcd.print("ms");
  
  Serial.println(F("\n=== FINAL STATISTICS ==="));
  Serial.print(F("P1 Avg: ")); Serial.print(p1Avg);
  Serial.print(F("ms | Best: ")); Serial.print(p1Best); Serial.println(F("ms"));
  Serial.print(F("P2 Avg: ")); Serial.print(p2Avg);
  Serial.print(F("ms | Best: ")); Serial.print(p2Best); Serial.println(F("ms"));
  Serial.print(F("Score: P1=")); Serial.print(p1Wins);
  Serial.print(F(" P2=")); Serial.println(p2Wins);
  
  // Print all round times
  Serial.println(F("--- Round-by-Round ---"));
  for (int i = 0; i < TOTAL_ROUNDS; i++) {
    Serial.print(F("R")); Serial.print(i + 1);
    Serial.print(F(": P1=")); Serial.print(p1Times[i]);
    Serial.print(F("ms")); 
    if (p1FalseStart[i]) Serial.print(F(" (FALSE START)"));
    Serial.print(F(" | P2=")); Serial.print(p2Times[i]);
    Serial.print(F("ms"));
    if (p2FalseStart[i]) Serial.print(F(" (FALSE START)"));
    Serial.println();
  }
  
  stateTimer = millis();
  gameState = STATE_SHOW_STATS;
}

void showGameOver() {
  lcd.clear();
  lcd.setCursor(0, 0);
  
  if (p1Wins > p2Wins) {
    lcd.print(" PLAYER 1 WINS!");
  } else if (p2Wins > p1Wins) {
    lcd.print(" PLAYER 2 WINS!");
  } else {
    lcd.print("  IT'S A TIE!  ");
  }
  
  lcd.setCursor(0, 1);
  lcd.print("P1:");
  lcd.print(p1Wins);
  lcd.print(" P2:");
  lcd.print(p2Wins);
  lcd.print(" Again?");
  
  gameState = STATE_GAME_OVER;
}

void showWelcome() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" REACTION-TIME  ");
  lcd.setCursor(0, 1);
  lcd.print("    ARENA!      ");
  
  stateTimer = millis();
  gameState = STATE_WELCOME;
}

// ============================================================
// I2C COMMUNICATION
// ============================================================

void sendCommandToPlayer(int address, char command) {
  Wire.beginTransmission(address);
  Wire.write(command);
  Wire.endTransmission();
  
  Serial.print(F("Sent '"));
  Serial.print(command);
  Serial.print(F("' to 0x"));
  Serial.println(address, HEX);
}

int requestReactionTime(int address) {
  // Request 2 bytes from player station
  Wire.requestFrom(address, 2);
  
  if (Wire.available() >= 2) {
    int highByte = Wire.read();
    int lowByte  = Wire.read();
    int reactionTime = (highByte << 8) | lowByte;
    
    // 0 means "not ready yet" (still timing)
    if (reactionTime == 0) return 0;
    
    return reactionTime;  // actual time in ms, or 9999 for false start
  }
  
  return 0;  // No data available yet
}

