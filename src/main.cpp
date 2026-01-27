#include <Arduino.h>
#include <ShiftRegister74HC595.h>
#include <SoftwareSerial.h>

// UART với ESP32
#define ARDUINO_RX 2
#define ARDUINO_TX 3
SoftwareSerial ESP32Serial(ARDUINO_RX, ARDUINO_TX);

// Pin definitions
#define LIGHT_GREEN1_PIN A0
#define LIGHT_YELLOW1_PIN A1
#define LIGHT_RED1_PIN A2
#define LIGHT_GREEN2_PIN A3
#define LIGHT_YELLOW2_PIN A4
#define LIGHT_RED2_PIN A5
#define STREET_LIGHT_PIN 6 // Đèn đường (hỗ trợ PWM)

// Create shift register objects with 2 registers each
ShiftRegister74HC595<2> sr(8, 9, 10);    // LED 7 đoạn 1
ShiftRegister74HC595<2> sr2(11, 12, 13); // LED 7 đoạn 2

uint8_t numberB[] = {
    B11000000, // 0
    B11111001, // 1
    B10100100, // 2
    B10110000, // 3
    B10011001, // 4
    B10010010, // 5
    B10000010, // 6
    B11111000, // 7
    B10000000, // 8
    B10010000  // 9
};

// Biến điều khiển từ ESP32
String currentMode = "auto";                       // "auto" hoặc "manual"
int autoRed = 40, autoYellow = 3, autoGreen = 37; // Thời gian auto mode
bool streetLightOn = false;
int streetBrightness = 0;

// Manual mode control
String manualJ1 = ""; // "RED", "YELLOW", "GREEN"
String manualJ2 = "";

// Biến đếm thời gian
unsigned long lastCycleTime = 0;
int countdown1 = 0; // Countdown cho Line 1 (LED sr)
int countdown2 = 0; // Countdown cho Line 2 (LED sr2)
int cyclePhase = 0; // 0: J1 ĐỎ/J2 XANH→VÀNG, 1: J1 XANH→VÀNG/J2 ĐỎ

void processUARTCommands();
void displayNumber(int num1, int num2);
void controlAutoMode();
void controlManualMode();


void setup() {
  // Serial cho debug
  Serial.begin(115200);
  
  // UART với ESP32
  ESP32Serial.begin(9600);
  
  Serial.println("\n=== Arduino Traffic Controller ===");
  Serial.println("Waiting for ESP32 commands...");

  pinMode(LIGHT_GREEN1_PIN, OUTPUT);
  pinMode(LIGHT_YELLOW1_PIN, OUTPUT);
  pinMode(LIGHT_RED1_PIN, OUTPUT);
  pinMode(LIGHT_RED2_PIN, OUTPUT);
  pinMode(LIGHT_YELLOW2_PIN, OUTPUT);
  pinMode(LIGHT_GREEN2_PIN, OUTPUT);
  pinMode(STREET_LIGHT_PIN, OUTPUT);

  // Tắt tất cả đèn ban đầu
  digitalWrite(LIGHT_GREEN1_PIN, LOW);
  digitalWrite(LIGHT_YELLOW1_PIN, LOW);
  digitalWrite(LIGHT_RED1_PIN, LOW);
  digitalWrite(LIGHT_GREEN2_PIN, LOW);
  digitalWrite(LIGHT_YELLOW2_PIN, LOW);
  digitalWrite(LIGHT_RED2_PIN, LOW);

  analogWrite(STREET_LIGHT_PIN, 0);

  lastCycleTime = millis();
  countdown1 = autoRed;   // J1 bắt đầu với ĐỎ 40
  countdown2 = autoGreen; // J2 bắt đầu với XANH 37
  cyclePhase = 0;
}

void loop() {
  // Đọc lệnh từ ESP32
  processUARTCommands();

  // Điều khiển đèn đường
  if (streetLightOn) {
    analogWrite(STREET_LIGHT_PIN, map(streetBrightness, 0, 100, 0, 255));
  } else {
    analogWrite(STREET_LIGHT_PIN, 0);
  }

  // Điều khiển đèn giao thông
  if (currentMode == "manual") {
    controlManualMode();
  } else {
    controlAutoMode();
  }
}

void processUARTCommands() {
  if (ESP32Serial.available()) {
    String cmd = ESP32Serial.readStringUntil('\n');
    cmd.trim();
    
    Serial.print("[ESP32 CMD] ");
    Serial.println(cmd);

    // STREET:ON:65 hoặc STREET:OFF
    if (cmd.startsWith("STREET:")) {
      if (cmd.indexOf("ON:") > 0) {
        int colonPos = cmd.lastIndexOf(':');
        streetBrightness = cmd.substring(colonPos + 1).toInt();
        streetLightOn = true;
        Serial.println("  -> Street Light ON at " + String(streetBrightness) + "%");
      } else if (cmd.indexOf("OFF") > 0) {
        streetLightOn = false;
        Serial.println("  -> Street Light OFF");
      }
    }

    // AUTO:NORMAL:30:3:27 hoặc AUTO:PEAK:45:3:42
    else if (cmd.startsWith("AUTO:")) {
      currentMode = "auto";

      int firstColon = cmd.indexOf(':', 5);
      int secondColon = cmd.indexOf(':', firstColon + 1);
      int thirdColon = cmd.indexOf(':', secondColon + 1);

      if (firstColon > 0 && secondColon > 0 && thirdColon > 0) {
        autoRed = cmd.substring(firstColon + 1, secondColon).toInt();
        autoYellow = cmd.substring(secondColon + 1, thirdColon).toInt();
        autoGreen = cmd.substring(thirdColon + 1).toInt();

        Serial.println("  -> AUTO mode: R=" + String(autoRed) + "s Y=" + String(autoYellow) + "s G=" + String(autoGreen) + "s");
        
        // Reset chu kỳ
        cyclePhase = 0;
        countdown1 = autoRed;
        countdown2 = autoGreen;
        lastCycleTime = millis();
      }
    }

    // MANUAL:J1:GREEN, MANUAL:J2:RED
    else if (cmd.startsWith("MANUAL:")) {
      currentMode = "manual";

      if (cmd.indexOf("J1:") > 0) {
        int colonPos = cmd.lastIndexOf(':');
        manualJ1 = cmd.substring(colonPos + 1);
        Serial.println("  -> MANUAL J1 = " + manualJ1);
      } else if (cmd.indexOf("J2:") > 0) {
        int colonPos = cmd.lastIndexOf(':');
        manualJ2 = cmd.substring(colonPos + 1);
        Serial.println("  -> MANUAL J2 = " + manualJ2);
      }
    }
  }
}

void displayNumber(int num1, int num2) {
  // Hiển thị num1 trên LED 7 đoạn 1 (sr)
  if (num1 < 0) num1 = 0;
  if (num1 > 99) num1 = 99;
  int digit2_1 = num1 % 10;
  int digit1_1 = (num1 / 10) % 10;
  uint8_t numberToPrint1[] = {numberB[digit1_1], numberB[digit2_1]};
  sr.setAll(numberToPrint1);
  
  // Hiển thị num2 trên LED 7 đoạn 2 (sr2)
  if (num2 < 0) num2 = 0;
  if (num2 > 99) num2 = 99;
  int digit2_2 = num2 % 10;
  int digit1_2 = (num2 / 10) % 10;
  uint8_t numberToPrint2[] = {numberB[digit1_2], numberB[digit2_2]};
  sr2.setAll(numberToPrint2);
}

void controlAutoMode() {
  // Countdown timer
  unsigned long currentTime = millis();
  if (currentTime - lastCycleTime >= 1000) {
    lastCycleTime = currentTime;
    countdown1--;
    countdown2--;

    // Xử lý Line 1 (J1)
    if (countdown1 < 0) {
      if (cyclePhase == 0) {
        // J1 ĐỎ kết thúc -> chuyển sang XANH
        cyclePhase = 1;
        countdown1 = autoGreen; // 37
      } else if (cyclePhase == 1) {
        // J1 XANH kết thúc -> chuyển sang VÀNG
        countdown1 = autoYellow - 1; // 2 (hiển thị 2,1,0)
      } else {
        // J1 VÀNG kết thúc -> chuyển sang ĐỎ
        cyclePhase = 0;
        countdown1 = autoRed; // 40
      }
    }
    
    // Xử lý Line 2 (J2) - ngược với J1
    if (countdown2 < 0) {
      if (cyclePhase == 0) {
        // J2 XANH kết thúc -> chuyển sang VÀNG
        countdown2 = autoYellow - 1; // 2
      } else if (cyclePhase == 1) {
        // J2 VÀNG kết thúc -> chuyển sang ĐỎ (J1 đang XANH)
        countdown2 = autoRed; // 40
      }
    }
  }

  // Hiển thị countdown riêng cho mỗi LED
  displayNumber(countdown1, countdown2);

  // Điều khiển đèn Line 1
  if (cyclePhase == 0) {
    // J1: ĐỎ
    digitalWrite(LIGHT_RED1_PIN, HIGH);
    digitalWrite(LIGHT_YELLOW1_PIN, LOW);
    digitalWrite(LIGHT_GREEN1_PIN, LOW);
  } else if (cyclePhase == 1 && countdown1 > autoYellow - 1) {
    // J1: XANH
    digitalWrite(LIGHT_GREEN1_PIN, HIGH);
    digitalWrite(LIGHT_YELLOW1_PIN, LOW);
    digitalWrite(LIGHT_RED1_PIN, LOW);
  } else {
    // J1: VÀNG
    digitalWrite(LIGHT_GREEN1_PIN, LOW);
    digitalWrite(LIGHT_YELLOW1_PIN, HIGH);
    digitalWrite(LIGHT_RED1_PIN, LOW);
  }
  
  // Điều khiển đèn Line 2 - ngược với Line 1
  if (cyclePhase == 1) {
    // J2: ĐỎ
    digitalWrite(LIGHT_RED2_PIN, HIGH);
    digitalWrite(LIGHT_YELLOW2_PIN, LOW);
    digitalWrite(LIGHT_GREEN2_PIN, LOW);
  } else if (cyclePhase == 0 && countdown2 > autoYellow - 1) {
    // J2: XANH
    digitalWrite(LIGHT_GREEN2_PIN, HIGH);
    digitalWrite(LIGHT_YELLOW2_PIN, LOW);
    digitalWrite(LIGHT_RED2_PIN, LOW);
  } else {
    // J2: VÀNG
    digitalWrite(LIGHT_GREEN2_PIN, LOW);
    digitalWrite(LIGHT_YELLOW2_PIN, HIGH);
    digitalWrite(LIGHT_RED2_PIN, LOW);
  }
}

void controlManualMode() {
  // Tắt đếm ngược trong manual mode
  displayNumber(0, 0);

  // Junction 1
  if (manualJ1 == "GREEN") {
    digitalWrite(LIGHT_GREEN1_PIN, HIGH);
    digitalWrite(LIGHT_YELLOW1_PIN, LOW);
    digitalWrite(LIGHT_RED1_PIN, LOW);
  } else if (manualJ1 == "YELLOW") {
    digitalWrite(LIGHT_GREEN1_PIN, LOW);
    digitalWrite(LIGHT_YELLOW1_PIN, HIGH);
    digitalWrite(LIGHT_RED1_PIN, LOW);
  } else if (manualJ1 == "RED") {
    digitalWrite(LIGHT_GREEN1_PIN, LOW);
    digitalWrite(LIGHT_YELLOW1_PIN, LOW);
    digitalWrite(LIGHT_RED1_PIN, HIGH);
  }

  // Junction 2
  if (manualJ2 == "GREEN") {
    digitalWrite(LIGHT_GREEN2_PIN, HIGH);
    digitalWrite(LIGHT_YELLOW2_PIN, LOW);
    digitalWrite(LIGHT_RED2_PIN, LOW);
  } else if (manualJ2 == "YELLOW") {
    digitalWrite(LIGHT_GREEN2_PIN, LOW);
    digitalWrite(LIGHT_YELLOW2_PIN, HIGH);
    digitalWrite(LIGHT_RED2_PIN, LOW);
  } else if (manualJ2 == "RED") {
    digitalWrite(LIGHT_GREEN2_PIN, LOW);
    digitalWrite(LIGHT_YELLOW2_PIN, LOW);
    digitalWrite(LIGHT_RED2_PIN, HIGH);
  }
}
