#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

LiquidCrystal_I2C lcd(0x20, 16, 2);
SoftwareSerial btSerial(10, 11);

// ============================================================
// 기존 핀 유지
// ============================================================
const int ESTOP_PIN = 2;
const int DEADMAN_PIN = 3;
const int MODE_PIN = 4;

// 0, 1, 2번 타깃만 사용
// 기존 5, 6, 7 유지 / 8번 제거
const int TARGET_COUNT = 3;
const int TARGET_BTN_PINS[TARGET_COUNT] = {5, 6, 7};

const int START_PIN = 9;
const int JOY_X_PIN = A0;
const int JOY_Y_PIN = A1;

// ============================================================
// 버튼 극성
// ============================================================
// START, MODE, DEADMAN, TARGET 버튼은 기존 코드처럼 INPUT_PULLUP 기준
// 안 누름 HIGH, 누름 LOW
const int BUTTON_ACTIVE_LEVEL = LOW;

// 기존 코드에서 E-STOP은 누르고 있을 때 estop=1이 떴으므로 HIGH 기준으로 둠.
// 만약 업로드 후 빨간 버튼을 눌러도 estop=1이 안 뜨면 HIGH를 LOW로 바꾸면 됨.
const int ESTOP_ACTIVE_LEVEL = HIGH;

// ============================================================
// 상태 변수
// ============================================================
int seq = 1;

// START는 초기 설정 완료 플래그
// 처음에는 start=0, START 누른 뒤에는 계속 start=1
bool start_latched = false;

// E-STOP latch
// E-STOP 한 번 누르면 estop=1 유지
// START 다시 누르면 estop=0 해제
bool estop_latched = false;

// START 이후 target 설정 잠금
bool target_config_locked = false;

// 기존 변수 유지
bool is_estop_locked = true;
bool is_manual_mode = false;
bool is_deadman_active = false;

bool target_states[TARGET_COUNT] = {false, false, false};

int last_start_state = HIGH;
int last_estop_state = LOW;
int last_mode_state = HIGH;
int last_deadman_state = HIGH;
int last_target_btn_states[TARGET_COUNT] = {HIGH, HIGH, HIGH};

// LCD 깨짐 방지용
unsigned long last_lcd_update_ms = 0;
String prev_lcd_line1 = "";
String prev_lcd_line2 = "";

String topst_state = "READY";
String topst_target = "";

// ============================================================
// target_mask 생성
// ============================================================
int makeTargetMask() {
  int target_mask = 0;

  if (target_states[0]) target_mask += 1;  // target 0
  if (target_states[1]) target_mask += 2;  // target 1
  if (target_states[2]) target_mask += 4;  // target 2

  return target_mask;
}

// ============================================================
// TOPST 상태 수신
// ============================================================
void readTopstStatus() {
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
}

// ============================================================
// LCD 출력
// ============================================================
void updateLcd() {
  if (millis() - last_lcd_update_ms < 500) {
    return;
  }
  last_lcd_update_ms = millis();

  String line1 = "Dest: ";
  bool anyTarget = false;

  for (int i = 0; i < TARGET_COUNT; i++) {
    if (target_states[i]) {
      line1 += String(i);
      line1 += " ";
      anyTarget = true;
    }
  }

  if (!anyTarget) {
    line1 += "NONE";
  }

  if (target_config_locked) {
    line1 += "L";
  }

  while (line1.length() < 16) line1 += " ";
  if (line1.length() > 16) line1 = line1.substring(0, 16);

  String mode_str = is_manual_mode ? "[MAN]" : "[AUTO]";
  String status_msg = "";

  if (estop_latched) {
    status_msg = "E-STOPPED";
  } else if (!start_latched) {
    status_msg = "SETUP";
  } else {
    if (topst_state == "READY") status_msg = "READY";
    else if (topst_state == "START") status_msg = "START";
    else if (topst_state == "MOVING") status_msg = "Move " + topst_target;
    else if (topst_state == "WAITING") status_msg = "WAITING";
    else if (topst_state == "COMPLETION") status_msg = "DONE";
    else if (topst_state == "ESTOPPED") status_msg = "E-STOPPED";
    else status_msg = topst_state;
  }

  String line2 = mode_str + " " + status_msg;
  while (line2.length() < 16) line2 += " ";
  if (line2.length() > 16) line2 = line2.substring(0, 16);

  if (line1 != prev_lcd_line1) {
    lcd.setCursor(0, 0);
    lcd.print(line1);
    prev_lcd_line1 = line1;
  }

  if (line2 != prev_lcd_line2) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
    prev_lcd_line2 = line2;
  }
}

// ============================================================
// setup
// ============================================================
void setup() {
  Serial.begin(115200);
  btSerial.begin(115200);
  btSerial.setTimeout(10);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  delay(100);

  lcd.setCursor(0, 0);
  lcd.print("SYSTEM BOOTING ");
  lcd.setCursor(0, 1);
  lcd.print("PLEASE WAIT    ");
  delay(1000);

  lcd.clear();
  delay(100);

  pinMode(ESTOP_PIN, INPUT_PULLUP);
  pinMode(DEADMAN_PIN, INPUT_PULLUP);
  pinMode(MODE_PIN, INPUT_PULLUP);
  pinMode(START_PIN, INPUT_PULLUP);

  for (int i = 0; i < TARGET_COUNT; i++) {
    pinMode(TARGET_BTN_PINS[i], INPUT_PULLUP);
  }

  last_estop_state = digitalRead(ESTOP_PIN);
}

// ============================================================
// loop
// ============================================================
void loop() {
  int current_estop = digitalRead(ESTOP_PIN);
  int current_start = digitalRead(START_PIN);
  int current_mode = digitalRead(MODE_PIN);
  int current_deadman = digitalRead(DEADMAN_PIN);

  bool start_pressed = (last_start_state == HIGH && current_start == BUTTON_ACTIVE_LEVEL);
  last_start_state = current_start;

  bool estop_pressed = (last_estop_state != ESTOP_ACTIVE_LEVEL && current_estop == ESTOP_ACTIVE_LEVEL);
  last_estop_state = current_estop;

  // ------------------------------------------------------------
  // E-STOP latch
  // ------------------------------------------------------------
  // E-STOP 한 번 누르면 START 다시 누르기 전까지 estop=1 유지
  if (estop_pressed) {
    estop_latched = true;
    is_manual_mode = false;
    is_deadman_active = false;
  }

  // ------------------------------------------------------------
  // START latch
  // ------------------------------------------------------------
  // START 전: target 선택 후 START를 눌러야 start=1
  // START 후: start=1 유지
  // E-STOP 후 START: estop=0 해제, start=1 유지
  if (start_pressed) {
    int target_mask_now = makeTargetMask();

    if (!start_latched) {
      if (target_mask_now != 0) {
        start_latched = true;
        target_config_locked = true;
        estop_latched = false;
      }
    } else {
      estop_latched = false;
    }
  }

  // 기존 코드 호환용
  is_estop_locked = estop_latched;

  // ------------------------------------------------------------
  // Target 선택
  // ------------------------------------------------------------
  // START 전 초기 설정 단계에서만 target 버튼 허용
  // START 이후에는 target 버튼 무시
  if (!target_config_locked) {
    for (int i = 0; i < TARGET_COUNT; i++) {
      int btn_state = digitalRead(TARGET_BTN_PINS[i]);

      if (last_target_btn_states[i] == HIGH && btn_state == BUTTON_ACTIVE_LEVEL) {
        target_states[i] = !target_states[i];
      }

      last_target_btn_states[i] = btn_state;
    }
  } else {
    for (int i = 0; i < TARGET_COUNT; i++) {
      last_target_btn_states[i] = digitalRead(TARGET_BTN_PINS[i]);
    }
  }

  // ------------------------------------------------------------
  // Manual mode toggle
  // ------------------------------------------------------------
  // START 이후, E-STOP이 아닐 때만 manual 전환 가능
  if (start_latched && !is_estop_locked &&
      last_mode_state == HIGH && current_mode == BUTTON_ACTIVE_LEVEL) {
    is_manual_mode = !is_manual_mode;

    if (!is_manual_mode) {
      is_deadman_active = false;
    }
  }
  last_mode_state = current_mode;

  // ------------------------------------------------------------
  // Deadman
  // ------------------------------------------------------------
  if (is_manual_mode && start_latched && !is_estop_locked) {
    if (last_deadman_state == HIGH && current_deadman == BUTTON_ACTIVE_LEVEL) {
      is_deadman_active = !is_deadman_active;
    }
  }
  last_deadman_state = current_deadman;

  // ------------------------------------------------------------
  // Joystick / deadman output
  // ------------------------------------------------------------
  int joyx = 512;
  int joyy = 512;

  // AUTO에서는 deadman=1
  // MANUAL에서는 deadman 버튼이 켜져야 1
  int deadman_out = 1;

  if (is_manual_mode && start_latched && !is_estop_locked) {
    joyx = 1023 - analogRead(JOY_X_PIN);
    joyy = 1023 - analogRead(JOY_Y_PIN);
    deadman_out = is_deadman_active ? 1 : 0;
  }

  if (is_estop_locked) {
    joyx = 512;
    joyy = 512;
    deadman_out = 0;
  }

  // ------------------------------------------------------------
  // TOPST 상태 수신
  // ------------------------------------------------------------
  readTopstStatus();

  // ------------------------------------------------------------
  // LCD
  // ------------------------------------------------------------
  updateLcd();

  // ------------------------------------------------------------
  // Payload
  // ------------------------------------------------------------
  int target_mask = makeTargetMask();

  String payload = "CTRL seq=" + String(seq++) +
                   " start=" + String(start_latched ? 1 : 0) +
                   " estop=" + String(estop_latched ? 1 : 0) +
                   " deadman=" + String(deadman_out) +
                   " manual=" + String(is_manual_mode ? 1 : 0) +
                   " target_mask=" + String(target_mask) +
                   " joyx=" + String(joyx) +
                   " joyy=" + String(joyy);

  Serial.println(payload);
  btSerial.println(payload);

  delay(100);
}
