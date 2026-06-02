#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

LiquidCrystal_I2C lcd(0x20, 16, 2); 
SoftwareSerial btSerial(10, 11);

const int ESTOP_PIN = 2;
const int DEADMAN_PIN = 3;
const int MODE_PIN = 4; 
const int TARGET_BTN_PINS[4] = {5, 6, 7, 8}; 
const int START_PIN = 9;
const int JOY_X_PIN = A0;
const int JOY_Y_PIN = A1;

int seq = 1;
bool is_estop_locked = true; 

int last_start_state = HIGH;
int last_mode_state = HIGH;
int last_deadman_state = HIGH;
int last_target_btn_states[4] = {HIGH, HIGH, HIGH, HIGH};

bool is_manual_mode = false; 
bool is_deadman_active = false; 
bool target_states[4] = {false, false, false, false}; 

String topst_state = "READY"; 
String topst_target = "";     

void setup() {
  Serial.begin(115200);
  btSerial.begin(115200);
  btSerial.setTimeout(10); 

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM BOOTING..");
  delay(1000);

  pinMode(ESTOP_PIN, INPUT_PULLUP);
  pinMode(DEADMAN_PIN, INPUT_PULLUP);
  pinMode(MODE_PIN, INPUT_PULLUP);
  pinMode(START_PIN, INPUT_PULLUP);

  for (int i = 0; i < 4; i++) {
    pinMode(TARGET_BTN_PINS[i], INPUT_PULLUP);
  }
}

void loop() {
  int current_estop = digitalRead(ESTOP_PIN);
  int current_start = digitalRead(START_PIN);
  int current_mode = digitalRead(MODE_PIN);
  int current_deadman = digitalRead(DEADMAN_PIN);

  bool start_pressed = (last_start_state == HIGH && current_start == LOW);
  last_start_state = current_start;

  if (!is_estop_locked && last_mode_state == HIGH && current_mode == LOW) {
    is_manual_mode = !is_manual_mode;
    if (!is_manual_mode) is_deadman_active = false; 
  }
  last_mode_state = current_mode;

  if (current_estop == HIGH) {
    is_estop_locked = true; 
  } else if (is_estop_locked && start_pressed) {
    is_estop_locked = false; 
  }

  if (!is_estop_locked) {
    for (int i = 0; i < 4; i++) {
      int btn_state = digitalRead(TARGET_BTN_PINS[i]);
      if (last_target_btn_states[i] == HIGH && btn_state == LOW) {
        target_states[i] = !target_states[i]; 
      }
      last_target_btn_states[i] = btn_state;
    }
  }

  if (is_manual_mode && !is_estop_locked) {
    if (last_deadman_state == HIGH && current_deadman == LOW) {
       is_deadman_active = !is_deadman_active;
    }
  }
  last_deadman_state = current_deadman;

  int joyx = 512, joyy = 512;
  int deadman_out = 0;
  
  if (is_manual_mode && !is_estop_locked) {
    // [수정됨] 조이스틱 물리적 180도 회전 반전 적용
    joyx = 1023 - analogRead(JOY_X_PIN);
    joyy = 1023 - analogRead(JOY_Y_PIN);
    deadman_out = is_deadman_active ? 1 : 0;
  }

  if (btSerial.available()) {
    String incoming = btSerial.readStringUntil('\n');
    incoming.trim();
    
    if (incoming.startsWith("STAT")) {
      int stateIndex = incoming.indexOf("state=");
      if (stateIndex != -1) {
        int spaceAfterState = incoming.indexOf(" ", stateIndex);
        if (spaceAfterState == -1) spaceAfterState = incoming.length();
        topst_state = incoming.substring(stateIndex + 6, spaceAfterState);
      }
      
      int targetIndex = incoming.indexOf("target=");
      if (targetIndex != -1) {
        int spaceAfterTarget = incoming.indexOf(" ", targetIndex);
        if (spaceAfterTarget == -1) spaceAfterTarget = incoming.length();
        topst_target = incoming.substring(targetIndex + 7, spaceAfterTarget);
      }
    }
  }

  String line1 = "Dest: ";
  for (int i = 0; i < 4; i++) {
    if (target_states[i]) {
      line1 += String(i) + " ";
    }
  }
  line1 += "4"; 
  while(line1.length() < 16) line1 += " "; 

  String mode_str = is_manual_mode ? "[MAN]" : "[AUTO]";
  String status_msg = "";

  if (is_estop_locked) {
    status_msg = "E-STOPPED"; 
  } else {
    if (topst_state == "READY") status_msg = "READY";
    else if (topst_state == "START") status_msg = "START";
    else if (topst_state == "MOVING") status_msg = "Move to " + topst_target;
    else if (topst_state == "WAITING") status_msg = "WAITING";
    else if (topst_state == "COMPLETION") status_msg = "COMPLETION";
    else if (topst_state == "ESTOPPED") status_msg = "E-STOPPED";
    else status_msg = topst_state;
  }

  String line2 = mode_str + " " + status_msg;
  while(line2.length() < 16) line2 += " "; 

  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  int target_mask = 0;
  if (target_states[0]) target_mask += 1; 
  if (target_states[1]) target_mask += 2; 
  if (target_states[2]) target_mask += 4; 
  if (target_states[3]) target_mask += 8; 

  String payload = "CTRL seq=" + String(seq++) + 
                   " start=" + String((start_pressed && !is_estop_locked) ? 1 : 0) +
                   " estop=" + String(is_estop_locked ? 1 : 0) +
                   " deadman=" + String(deadman_out) +
                   " target_mask=" + String(target_mask) +
                   " joyx=" + String(joyx) +
                   " joyy=" + String(joyy);

  Serial.println(payload);   
  btSerial.println(payload); 

  delay(100); 
}
