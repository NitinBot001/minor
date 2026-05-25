// ================================================================
// Arduino UNO — MASTER UI ENGINE (100% COMPLETE)
// Features: Correct Touch Mapping, 8-Sample Fast Vitals, ECG Graph
// ================================================================

#include <LCDWIKI_GUI.h>
#include <LCDWIKI_KBV.h>
#include <TouchScreen.h>
#include <SoftwareSerial.h>

LCDWIKI_KBV my_lcd(240, 320, A3, A2, A1, A0, A4); 
TouchScreen ts = TouchScreen(6, A1, A2, 7, 300);
SoftwareSerial espSerial(A5, 1); 

#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define YELLOW  0xFFE0
#define ORANGE  0xFD20
#define DGRAY   0x2104

enum Page { PAGE_TRANSIT, PAGE_WELCOME, 
            INST_HR, TEST_HR, RES_HR, 
            INST_SPO2, TEST_SPO2, RES_SPO2, 
            INST_TEMP, TEST_TEMP, RES_TEMP, 
            INST_ECG, TEST_ECG, RES_ECG, 
            PAGE_REPORT };

Page currentPage = PAGE_TRANSIT; 
bool pageChanged = true;

int currentBed = 1;
int liveBpm = 0, liveSpo2 = 0, liveEcg = 0;
float liveTemp = 0.0;

int sampleCount = 0; float sampleSum = 0; unsigned long lastSampleTime = 0;
int finalBpm = 0, finalSpo2 = 0; float finalTemp = 0.0;

// ECG Variables
#define GX1 10
#define GX2 310    
#define GY1 60
#define GY2 210
uint8_t ecgX = GX1;
int16_t lastEcgY = 135;
unsigned long ecgStartTime = 0;

char serialBuf[64];
uint8_t serialIdx = 0;

bool isTouched(int tx, int ty, int bx, int by, int bw, int bh) { return (tx >= bx && tx <= bx + bw && ty >= by && ty <= by + bh); }

void setup() {
  my_lcd.Init_LCD();
  my_lcd.Set_Rotation(1); 
  espSerial.begin(9600);
}

void loop() {
  readSerial();
  handleTouch();
  if (pageChanged) { drawCurrentPage(); pageChanged = false; }

  // FAST LOGIC PROCESSING (Reduced limits for quick measurement)
  if (currentPage == TEST_HR) processAveraging(liveBpm, 40); 
  else if (currentPage == TEST_SPO2) processAveraging(liveSpo2, 60); 
  else if (currentPage == TEST_TEMP) processAveragingFloat(liveTemp, 10.0); 
  else if (currentPage == TEST_ECG) processEcg();
}

void readSerial() {
  while (espSerial.available()) {
    char c = (char)espSerial.read();
    if (c == '\n') {
      serialBuf[serialIdx] = '\0';
      String data = String(serialBuf);
      if (data.startsWith("CMD:ARRIVED")) {
        int comma = data.indexOf(',');
        currentBed = data.substring(comma + 1).toInt();
        currentPage = PAGE_WELCOME; pageChanged = true;
      } 
      else if (data.startsWith("B:")) {
        char* p;
        p = strstr(serialBuf, "B:"); if (p) liveBpm = atoi(p + 2);
        p = strstr(serialBuf, "S:"); if (p) liveSpo2 = atoi(p + 2);
        p = strstr(serialBuf, "T:"); if (p) liveTemp = atof(p + 2);
        p = strstr(serialBuf, "E:"); if (p) liveEcg = atoi(p + 2);
      }
      serialIdx = 0;
    } else if (serialIdx < 63) serialBuf[serialIdx++] = c;
  }
}

// ===================== DRAWING LOGIC =====================
void drawButton(int x, int y, int w, int h, uint16_t color, String text) {
  my_lcd.Fill_Rect(x, y, w, h, color); 
  my_lcd.Set_Text_colour(WHITE); my_lcd.Set_Text_Back_colour(color); my_lcd.Set_Text_Size(2);
  int textWidth = text.length() * 12; 
  my_lcd.Print_String(text, x + (w - textWidth) / 2, y + (h - 16) / 2);
}

void drawInstructPage(String title, String step1, String step2) {
  my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_colour(CYAN); my_lcd.Set_Text_Size(3);
  my_lcd.Print_String(title, 20, 10);
  my_lcd.Set_Text_colour(WHITE); my_lcd.Set_Text_Size(2);
  my_lcd.Print_String("Instructions:", 10, 60);
  my_lcd.Set_Text_colour(YELLOW); 
  my_lcd.Print_String(step1, 10, 100);
  if(step2 != "") my_lcd.Print_String(step2, 10, 130);
  drawButton(60, 180, 200, 45, GREEN, "START TEST");
}

void drawResultPage(String title, String resultText) {
  my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_colour(GREEN); my_lcd.Set_Text_Size(3);
  my_lcd.Print_String(title + " DONE", 20, 30);
  my_lcd.Set_Text_colour(WHITE); my_lcd.Set_Text_Size(3);
  my_lcd.Print_String(resultText, 50, 100);
  drawButton(10, 180, 130, 45, ORANGE, "RETEST");
  drawButton(150, 180, 160, 45, BLUE, "NEXT STEP");
}

void drawCurrentPage() {
  if (currentPage == PAGE_TRANSIT) {
    my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_Size(3); my_lcd.Set_Text_colour(WHITE);
    my_lcd.Print_String("MOVING TO", 80, 80); my_lcd.Set_Text_colour(GREEN);
    my_lcd.Print_String("NEXT PATIENT...", 30, 130);
  }
  else if (currentPage == PAGE_WELCOME) {
    my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_Size(3); my_lcd.Set_Text_colour(CYAN);
    my_lcd.Print_String("HELLO PATIENT " + String(currentBed), 30, 40);
    drawButton(60, 100, 200, 50, GREEN, "START VITALS");
    drawButton(60, 170, 200, 40, DGRAY, "SKIP / NEXT");
  }
  else if (currentPage == INST_HR) drawInstructPage("HEART RATE", "Place your finger firmly", "on the Red Sensor block.");
  else if (currentPage == TEST_HR) { my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_colour(CYAN); my_lcd.Set_Text_Size(3); my_lcd.Print_String("TESTING HR...", 60, 20); sampleCount = 0; sampleSum = 0; lastSampleTime = millis(); }
  else if (currentPage == RES_HR) drawResultPage("HEART RATE", "HR: " + String(finalBpm) + " BPM");
  
  else if (currentPage == INST_SPO2) drawInstructPage("OXYGEN LEVEL", "Keep your finger steady", "on the Red Sensor.");
  else if (currentPage == TEST_SPO2) { my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_colour(CYAN); my_lcd.Set_Text_Size(3); my_lcd.Print_String("TESTING SPO2...", 40, 20); sampleCount = 0; sampleSum = 0; lastSampleTime = millis(); }
  else if (currentPage == RES_SPO2) drawResultPage("OXYGEN LEVEL", "SpO2: " + String(finalSpo2) + " %");
  
  else if (currentPage == INST_TEMP) drawInstructPage("TEMPERATURE", "Place the metal sensor", "under your armpit.");
  else if (currentPage == TEST_TEMP) { my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_colour(CYAN); my_lcd.Set_Text_Size(3); my_lcd.Print_String("TESTING TEMP...", 40, 20); sampleCount = 0; sampleSum = 0; lastSampleTime = millis(); }
  else if (currentPage == RES_TEMP) drawResultPage("TEMPERATURE", "Temp: " + String(finalTemp, 1) + " C");

  else if (currentPage == INST_ECG) drawInstructPage("ECG RECORDING", "Attach Leads: L+ (Right),", "L- (Left). Relax & breath.");
  else if (currentPage == TEST_ECG) {
    my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_colour(CYAN); my_lcd.Set_Text_Size(2);
    my_lcd.Print_String("Recording 20s ECG...", 40, 10);
    my_lcd.Fill_Rect(GX1, GY1, GX2 - GX1, GY2 - GY1, DGRAY);
    ecgX = GX1; lastEcgY = 135;
    espSerial.println("CMD:START_ECG"); 
    ecgStartTime = millis();
  }
  else if (currentPage == RES_ECG) {
    my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_colour(GREEN); my_lcd.Set_Text_Size(3);
    my_lcd.Print_String("ECG SAVED!", 80, 50);
    drawButton(10, 180, 130, 45, ORANGE, "RETEST"); drawButton(150, 180, 160, 45, BLUE, "FINAL REPORT");
  }
  else if (currentPage == PAGE_REPORT) {
    my_lcd.Fill_Screen(BLACK); my_lcd.Set_Text_colour(WHITE); my_lcd.Set_Text_Size(2);
    my_lcd.Print_String("FINAL REPORT - BED " + String(currentBed), 20, 10);
    my_lcd.Set_Text_colour(CYAN);
    my_lcd.Print_String("HR   : " + String(finalBpm) + " BPM", 20, 60);
    my_lcd.Print_String("SpO2 : " + String(finalSpo2) + " %", 20, 95);
    my_lcd.Print_String("Temp : " + String(finalTemp,1) + " C", 20, 130);
    my_lcd.Print_String("ECG  : 20s Traced", 20, 165);
    drawButton(10, 200, 300, 35, BLUE, "SYNC & NEXT PATIENT"); 
  }
}

// ===================== FAST AVERAGING LOGIC (8 SAMPLES) =====================
void processAveraging(int val, int ignoreThr) {
  my_lcd.Set_Text_Back_colour(BLACK); my_lcd.Set_Text_colour(WHITE); my_lcd.Set_Text_Size(5);
  my_lcd.Print_Number_Int(val, 120, 80, 3, ' ', 10); 
  
  // Timing reduced to 400ms gap for speed
  if (millis() - lastSampleTime >= 400) { 
    lastSampleTime = millis();
    if (val > ignoreThr) { 
      sampleSum += val; sampleCount++;
      my_lcd.Set_Text_colour(YELLOW); my_lcd.Set_Text_Size(2);
      
      // Limit to 8 samples
      String msg = " Samples: " + String(sampleCount) + "/8 "; while(msg.length() < 22) msg += " "; 
      my_lcd.Print_String(msg, 50, 160);
      
      if (sampleCount >= 8) {
        if(currentPage == TEST_HR) { finalBpm = sampleSum/8.0; currentPage = RES_HR; }
        if(currentPage == TEST_SPO2) { finalSpo2 = sampleSum/8.0; currentPage = RES_SPO2; }
        pageChanged = true;
      }
    } else {
      my_lcd.Set_Text_colour(RED); my_lcd.Set_Text_Size(2);
      my_lcd.Print_String(" Waiting for sensor... ", 30, 160);
    }
  }
}

void processAveragingFloat(float val, float ignoreThr) {
  my_lcd.Set_Text_Back_colour(BLACK); my_lcd.Set_Text_colour(WHITE); my_lcd.Set_Text_Size(5);
  my_lcd.Print_Number_Float(val, 1, 100, 80, '.', 0, ' ');
  
  if (millis() - lastSampleTime >= 400) {
    lastSampleTime = millis();
    if (val > ignoreThr) { 
      sampleSum += val; sampleCount++;
      my_lcd.Set_Text_colour(YELLOW); my_lcd.Set_Text_Size(2);
      
      String msg = " Samples: " + String(sampleCount) + "/8 "; while(msg.length() < 22) msg += " "; 
      my_lcd.Print_String(msg, 50, 160);
      
      if (sampleCount >= 8) { finalTemp = sampleSum/8.0; currentPage = RES_TEMP; pageChanged = true; }
    } else {
      my_lcd.Set_Text_colour(RED); my_lcd.Set_Text_Size(2);
      my_lcd.Print_String(" Waiting for sensor... ", 30, 160); 
    }
  }
}

void processEcg() {
  int ecgY = map(liveEcg, 0, 4095, GY2 - 1, GY1 + 1); ecgY = constrain(ecgY, GY1 + 1, GY2 - 1);
  my_lcd.Set_Draw_color(GREEN);
  if (ecgX > GX1) my_lcd.Draw_Line(ecgX - 1, lastEcgY, ecgX, ecgY);
  if (ecgX + 1 < GX2) my_lcd.Fill_Rect(ecgX + 1, GY1, 4, GY2 - GY1, DGRAY); 
  lastEcgY = ecgY; ecgX++;
  if (ecgX >= GX2) { ecgX = GX1; my_lcd.Fill_Rect(GX1, GY1, GX2 - GX1, GY2 - GY1, DGRAY); }
  
  int elapsedSec = (millis() - ecgStartTime) / 1000;
  my_lcd.Set_Text_Back_colour(BLACK); my_lcd.Set_Text_colour(YELLOW); my_lcd.Set_Text_Size(2);
  my_lcd.Print_String(String(20 - elapsedSec) + "s remaining ", 80, 215);

  if (millis() - ecgStartTime >= 20000) { currentPage = RES_ECG; pageChanged = true; }
}

// ===================== TOUCH LOGIC (Correct Mapped) =====================
void handleTouch() {
  TSPoint p = ts.getPoint(); pinMode(A2, OUTPUT); pinMode(A1, OUTPUT);
  if (p.z > 50 && p.z < 1000) {
    int px = map(p.y, 900, 120, 0, 320); 
    int py = map(p.x, 900, 150, 0, 240); 

    if (currentPage == PAGE_WELCOME) {
      if (isTouched(px, py, 60, 100, 200, 50)) { currentPage = INST_HR; pageChanged = true; }
      if (isTouched(px, py, 60, 170, 200, 40)) { 
        espSerial.println("CMD:NEXT"); 
        currentPage = PAGE_TRANSIT; pageChanged = true; 
      }
    }
    else if (currentPage == INST_HR) { if(isTouched(px, py, 60, 180, 200, 45)) {currentPage = TEST_HR; pageChanged = true;} }
    else if (currentPage == INST_SPO2) { if(isTouched(px, py, 60, 180, 200, 45)) {currentPage = TEST_SPO2; pageChanged = true;} }
    else if (currentPage == INST_TEMP) { if(isTouched(px, py, 60, 180, 200, 45)) {currentPage = TEST_TEMP; pageChanged = true;} }
    else if (currentPage == INST_ECG) { if(isTouched(px, py, 60, 180, 200, 45)) {currentPage = TEST_ECG; pageChanged = true;} }
    
    else if (currentPage == RES_HR) {
      if (isTouched(px, py, 10, 180, 130, 45)) {currentPage = INST_HR; pageChanged = true;}
      if (isTouched(px, py, 150, 180, 160, 45)) {currentPage = INST_SPO2; pageChanged = true;}
    }
    else if (currentPage == RES_SPO2) {
      if (isTouched(px, py, 10, 180, 130, 45)) {currentPage = INST_SPO2; pageChanged = true;}
      if (isTouched(px, py, 150, 180, 160, 45)) {currentPage = INST_TEMP; pageChanged = true;}
    }
    else if (currentPage == RES_TEMP) {
      if (isTouched(px, py, 10, 180, 130, 45)) {currentPage = INST_TEMP; pageChanged = true;}
      if (isTouched(px, py, 150, 180, 160, 45)) {currentPage = INST_ECG; pageChanged = true;}
    }
    else if (currentPage == RES_ECG) {
      if (isTouched(px, py, 10, 180, 130, 45)) {currentPage = INST_ECG; pageChanged = true;}
      if (isTouched(px, py, 150, 180, 160, 45)) {currentPage = PAGE_REPORT; pageChanged = true;}
    }
    
    else if (currentPage == PAGE_REPORT) {
      if (isTouched(px, py, 10, 200, 300, 35)) {
        drawButton(10, 200, 300, 35, ORANGE, "UPLOADING...");
        
        espSerial.print("SYNC:"); espSerial.print(currentBed); espSerial.print(",");
        espSerial.print(finalBpm); espSerial.print(",");
        espSerial.print(finalSpo2); espSerial.print(",");
        espSerial.println(finalTemp);
        
        delay(2000); 
        
        espSerial.println("CMD:NEXT");
        finalBpm = 0; finalSpo2 = 0; finalTemp = 0.0;
        currentPage = PAGE_TRANSIT;
        pageChanged = true;
      }
    }
    delay(250); 
  }
}
