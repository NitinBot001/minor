// ================================================================
// ROVER ESP32 — Line Following + Autonomous Halt Protocol
// Base: User's Tuned PID Logic
// ================================================================

// ---------- 5 IR SENSORS (Left to Right) ----------
#define S1 15 // Extreme Left
#define S2 16 // Mid Left
#define S3 17 // Center
#define S4 18 // Mid Right
#define S5 19 // Extreme Right

// ---------- L298N MOTOR PINS ----------
#define ENA 21
#define IN1 22
#define IN2 23
#define ENB 25
#define IN3 26
#define IN4 27

// ---------- HANDSHAKE PINS (Mother ESP32) ----------
#define BED_REACHED_PIN 4  // Output to Mother (I arrived)
#define MOVE_NEXT_PIN 5    // Input from Mother (Go next)

// ---------- PID gains ----------
float Kp = 42.0;
float Ki = 0.0;
float Kd = 15.0;

const float INTEGRAL_MIN = -20.0;
const float INTEGRAL_MAX = 20.0;
const unsigned long SAMPLE_MS = 20;
const float ERROR_DEADBAND = 0.19;
const float SMOOTH_ALPHA = 0.5;
const int BASE_SPEED = 80;

// Pivot tuning
const float KP_PIVOT = 0.96;
const int PIVOT_INNER_POWER = 70;
const int PIVOT_OUTER_POWER = 150;
const unsigned long PRE_TURN_DELAY_MS = 150;
const unsigned long CENTER_CONFIRM_MS = 30;
const unsigned long EXTRA_CENTER_IGNORE_MS = 180;
const unsigned long FORCED_PIVOT_TIMEOUT_MS = 1000;

// ---------- SMART RECOVERY CONSTANTS ----------
const unsigned long RECOVERY_PHASE1_MS = 200;
const unsigned long RECOVERY_PHASE2_MS = 500;
const float RECOVERY_PHASE1_ERROR_SCALE = 1.2;
const float RECOVERY_PHASE2_ERROR = 2.5;
const float RECOVERY_PHASE3_ERROR = 3.5;

// PID state
float error = 0.0, lastError = 0.0, integral = 0.0, derivative = 0.0, smoothedCorrection = 0.0;
unsigned long lastSampleTime = 0;

// ---------- RECOVERY & HALT STATE ----------
enum LineSide { LINE_LEFT, LINE_CENTER, LINE_RIGHT };
LineSide lastKnownSide = LINE_CENTER;
bool inRecovery = false;
unsigned long recoveryStartTime = 0;
bool atBed = false; // NEW: Track if waiting at patient bed

// center-ignore helper
unsigned long centerIgnoreUntil = 0;

bool readCenter() {
  if (millis() < centerIgnoreUntil) return false;
  return digitalRead(S3);
}

void disableCenterFor(unsigned long ms) {
  centerIgnoreUntil = millis() + ms;
}

void setup() {
  pinMode(S1, INPUT); pinMode(S2, INPUT); pinMode(S3, INPUT);
  pinMode(S4, INPUT); pinMode(S5, INPUT);
  pinMode(ENA, OUTPUT); pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  
  // Handshake setup
  pinMode(BED_REACHED_PIN, OUTPUT);
  pinMode(MOVE_NEXT_PIN, INPUT_PULLDOWN);
  digitalWrite(BED_REACHED_PIN, LOW);

  stopMotors();
  delay(100);
  lastSampleTime = millis();
}

void loop() {
  // ----------------------------------------------------
  // NEW: WAIT AT BED LOGIC
  // ----------------------------------------------------
  if (atBed) {
    stopMotors();
    // Mother ESP32 se "NEXT PATIENT" ka command aaya kya?
    if (digitalRead(MOVE_NEXT_PIN) == HIGH) {
      digitalWrite(BED_REACHED_PIN, LOW); // Signal reset
      atBed = false;
      
      // Rover ko thoda aage badhao taaki wo bed ki intersection line cross kar le
      // Warna wo wahin ghoomta rahega ya wapas atBed = true ho jayega
      setMotorLeft(BASE_SPEED);
      setMotorRight(BASE_SPEED);
      delay(400); // 400ms tak seedhe aage. Agar line na paar ho, toh ise badha dena.
      
      // Reset PID variables taaki achanak jerk na aaye
      smoothedCorrection = 0.0; integral = 0.0; lastError = 0.0;
      lastSampleTime = millis();
    }
    return; // Wait karne ke dauran baki code run nahi hoga
  }

  // ----------------------------------------------------
  // YOUR ORIGINAL LINE FOLLOWER LOGIC BELOW
  // ----------------------------------------------------
  bool llRaw = digitalRead(S1);
  bool lRaw  = digitalRead(S2);
  bool cRaw  = readCenter();   
  bool rRaw  = digitalRead(S4);
  bool rrRaw = digitalRead(S5);

  // 1. Intersection — सब ON (Modified to trigger atBed)
  if (llRaw && lRaw && cRaw && rRaw && rrRaw) {
    stopMotors();
    inRecovery = false;
    
    // Naya bed mil gaya!
    atBed = true;
    digitalWrite(BED_REACHED_PIN, HIGH); // Signal Mother ESP32
    return;
  }

  // 2. Left 90° pivot
  else if (llRaw && lRaw && !rRaw && !rrRaw) {
    stopMotors();
    delay(PRE_TURN_DELAY_MS);
    forcedAggressivePivotLeft();
    smoothedCorrection = 0.0; integral = 0.0; lastError = 0.0;
    inRecovery = false;
    lastSampleTime = millis();
    return;
  }

  // 3. Right 90° pivot
  else if (rrRaw && rRaw && !lRaw && !llRaw) {
    stopMotors();
    delay(PRE_TURN_DELAY_MS);
    forcedAggressivePivotRight();
    smoothedCorrection = 0.0; integral = 0.0; lastError = 0.0;
    inRecovery = false;
    lastSampleTime = millis();
    return;
  }

  // 4. Normal PID + Smart Recovery
  unsigned long now = millis();
  if (now - lastSampleTime < SAMPLE_MS) return;
  float dt = (now - lastSampleTime) / 1000.0;
  lastSampleTime = now;
  if (dt <= 0) dt = 0.001;

  bool LL = digitalRead(S1);
  bool L  = digitalRead(S2);
  bool C  = readCenter();
  bool R  = digitalRead(S4);
  bool RR = digitalRead(S5);

  bool lineLost = (!LL && !L && !C && !R && !RR);

  if (!lineLost) {
    inRecovery = false;

    if      (!LL && !L &&  C && !R && !RR) error =  0.0;
    else if (!LL &&  L &&  C && !R && !RR) error = -0.5;
    else if (!LL &&  L && !C && !R && !RR) error = -1.0;
    else if ( LL &&  L && !C && !R && !RR) error = -1.5;
    else if ( LL && !L && !C && !R && !RR) error = -2.0;
    else if (!LL && !L &&  C &&  R && !RR) error =  0.5;
    else if (!LL && !L && !C &&  R && !RR) error =  1.0;
    else if (!LL && !L && !C &&  R &&  RR) error =  1.5;
    else if (!LL && !L && !C && !R &&  RR) error =  2.0;
    else error = 0.0;

    if      (error >  0.5) lastKnownSide = LINE_RIGHT;
    else if (error < -0.5) lastKnownSide = LINE_LEFT;
    else                   lastKnownSide = LINE_CENTER;

  } else {
    if (!inRecovery) {
      inRecovery = true;
      recoveryStartTime = now;
    }

    unsigned long lostFor = now - recoveryStartTime;

    if (lostFor < RECOVERY_PHASE1_MS) {
      error = lastError * RECOVERY_PHASE1_ERROR_SCALE;

    } else if (lostFor < RECOVERY_PHASE2_MS) {
      if      (lastKnownSide == LINE_RIGHT)  error =  RECOVERY_PHASE2_ERROR;
      else if (lastKnownSide == LINE_LEFT)   error = -RECOVERY_PHASE2_ERROR;
      else    error = (lastError >= 0) ? RECOVERY_PHASE2_ERROR : -RECOVERY_PHASE2_ERROR;

    } else {
      smoothedCorrection = 0.0;
      if      (lastKnownSide == LINE_RIGHT)  error =  RECOVERY_PHASE3_ERROR;
      else if (lastKnownSide == LINE_LEFT)   error = -RECOVERY_PHASE3_ERROR;
      else    error = (lastError >= 0) ? RECOVERY_PHASE3_ERROR : -RECOVERY_PHASE3_ERROR;
    }
  }

  if (fabs(error) < ERROR_DEADBAND && !lineLost) error = 0.0;

  integral += error * dt;
  if (integral > INTEGRAL_MAX) integral = INTEGRAL_MAX;
  if (integral < INTEGRAL_MIN) integral = INTEGRAL_MIN;

  derivative = (error - lastError) / dt;
  float rawCorrection = (Kp * error) + (Ki * integral) + (Kd * derivative);
  smoothedCorrection = SMOOTH_ALPHA * smoothedCorrection + (1.0 - SMOOTH_ALPHA) * rawCorrection;

  int leftPWM  = (int)round(BASE_SPEED + smoothedCorrection);
  int rightPWM = (int)round(BASE_SPEED - smoothedCorrection);

  setMotorLeft(leftPWM);
  setMotorRight(rightPWM);

  lastError = error;
}

// ---------- Aggressive pivots ----------
void forcedAggressivePivotLeft() {
  int innerPower = constrain((int)(PIVOT_INNER_POWER * KP_PIVOT), 0, 255);
  int outerPower = constrain((int)(PIVOT_OUTER_POWER * KP_PIVOT), 0, 255);
  setMotorLeft(-innerPower);
  setMotorRight(outerPower);
  unsigned long start = millis();
  bool centerWasHighAtStart = digitalRead(S3);
  bool centerFell = false;
  while (millis() - start < FORCED_PIVOT_TIMEOUT_MS) {
    bool centerNow = digitalRead(S3);
    if (centerWasHighAtStart) {
      if (!centerNow) centerFell = true;
      if (centerFell && confirmCenterRaw()) break;
    } else {
      if (confirmCenterRaw()) break;
    }
    delay(2);
  }
  stopMotors();
  delay(12);
  disableCenterFor(EXTRA_CENTER_IGNORE_MS);
}

void forcedAggressivePivotRight() {
  int innerPower = constrain((int)(PIVOT_INNER_POWER * KP_PIVOT), 0, 255);
  int outerPower = constrain((int)(PIVOT_OUTER_POWER * KP_PIVOT), 0, 255);
  setMotorRight(-innerPower);
  setMotorLeft(outerPower);
  unsigned long start = millis();
  bool centerWasHighAtStart = digitalRead(S3);
  bool centerFell = false;
  while (millis() - start < FORCED_PIVOT_TIMEOUT_MS) {
    bool centerNow = digitalRead(S3);
    if (centerWasHighAtStart) {
      if (!centerNow) centerFell = true;
      if (centerFell && confirmCenterRaw()) break;
    } else {
      if (confirmCenterRaw()) break;
    }
    delay(2);
  }
  stopMotors();
  delay(12);
  disableCenterFor(EXTRA_CENTER_IGNORE_MS);
}

bool confirmCenterRaw() {
  unsigned long t = millis();
  while (millis() - t < CENTER_CONFIRM_MS) {
    if (!digitalRead(S3)) return false;
    delay(2);
  }
  return true;
}

// ---------- Motor helpers ----------
void setMotorLeft(int pwm) {
  if (pwm >= 0) {
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
    analogWrite(ENA, constrain(pwm, 0, 255));
  } else {
    digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
    analogWrite(ENA, constrain(-pwm, 0, 255));
  }
}

void setMotorRight(int pwm) {
  if (pwm >= 0) {
    digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
    analogWrite(ENB, constrain(pwm, 0, 255));
  } else {
    digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
    analogWrite(ENB, constrain(-pwm, 0, 255));
  }
}

void stopMotors() {
  analogWrite(ENA, 0); digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  analogWrite(ENB, 0); digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}
