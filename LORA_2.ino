#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include <RTClib.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

RTC_DS1307 rtc;

LiquidCrystal_I2C lcd(0x27, 20, 4);

HardwareSerial LoRaSerial(2);
#define LORA_RX 16
#define LORA_TX 17

const char *ssid = "Fuvitech";
const char *password = "fuvitech.vn";

// MQTT settings
const char *mqtt_server = "mqtt.fuvitech.vn";
const int mqtt_port = 1883;

// MQTT Topics (backup)
const char *mqtt_topic_brightness =
    "trafficlight/streetlight/brightness/status";
const char *mqtt_topic_schedule = "trafficlight/streetlight/schedule/status";
const char *mqtt_topic_mode = "trafficlight/control/mode/status";
const char *mqtt_topic_junction1 = "trafficlight/status/junction1";
const char *mqtt_topic_junction2 = "trafficlight/status/junction2";

// Auto mode topics
const char *mqtt_topic_normal = "trafficlight/auto/normal/status";
const char *mqtt_topic_peak = "trafficlight/auto/peak/status";
const char *mqtt_topic_peak_schedule = "trafficlight/auto/schedule/set";

WiFiClient espClient;
PubSubClient client(espClient);

// Biến lưu trữ data
String operationMode = "";
int brightnessValue = 0;
String timeOn = "";
String timeOff = "";
String junction1Color = "";
String junction2Color = "";

// Auto mode timing
int normalRed = 0, normalYellow = 0, normalGreen = 0;
int peakRed = 0, peakYellow = 0, peakGreen = 0;
String mornStart = "", mornEnd = "";
String afterStart = "", afterEnd = "";
String evenStart = "", evenEnd = "";

// Biến theo dõi trạng thái đã gửi (tránh gửi lặp lại)
String lastSentMode = "";
int lastSentRed = 0, lastSentYellow = 0, lastSentGreen = 0;
String lastSentJ1 = "", lastSentJ2 = "";
bool lastStreetState = false;
int lastStreetBright = 0;
bool manualStreetOn = false;  // Flag bật/tắt thủ công bằng nút nhấn

// Biến đánh dấu nguồn dữ liệu
unsigned long lastLoRaReceive = 0;
unsigned long loraTimeout = 10000; // 10s không nhận LoRa → chuyển MQTT

// UART với Arduino
HardwareSerial ArduinoSerial(1); // Serial1
#define ARDUINO_TX 33
#define ARDUINO_RX 25

// Định nghĩa các nút nhấn
#define BTN_MODE     4   // Chuyển mode Manual <-> Auto
#define BTN_MENU     5   // Cycle qua menu settings
#define BTN_UP      18   // Tăng giá trị
#define BTN_DOWN    19   // Giảm giá trị

// Biến chống dội nút nhấn
unsigned long lastBtnPress[4] = {0, 0, 0, 0};
const unsigned long debounceDelay = 300;

// Biến menu
int  currentMenu    = 0;     // Menu đang chọn
bool inMenu         = false; // Đang ở trong menu
unsigned long menuLastAction = 0;
const unsigned long menuTimeoutMs = 5000; // 5s không nhấn -> thoát menu

// Giờ bật/tắt đèn đường (lưu dạng phút từ 00:00)
int streetOnMinutes  = 18 * 60; // 18:00
int streetOffMinutes =  6 * 60; // 06:00

// Timing đèn giao thông chỉnh bằng nút
int btnGreen  = 37;
int btnRed    = 40;
int btnYellow =  3;

// Số menu theo mode
#define MENU_MANUAL_COUNT 3
#define MENU_AUTO_COUNT   7

void setup() {
  // Serial cho debug
  Serial.begin(9600);
  delay(1000);
  // Cấu hình các nút nhấn
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  
  Serial.println("Buttons initialized");
  Serial.println("\n=== ESP32 LORA_2 - Display Node ===");

  // UART với Arduino (TX=33, RX=25, baud 9600)
  ArduinoSerial.begin(9600, SERIAL_8N1, ARDUINO_RX, ARDUINO_TX);

  // Khởi tạo I2C
  Wire.begin(21, 22);

  // Khởi tạo LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  LORA_2 Display  ");
  lcd.setCursor(0, 1);
  lcd.print("   Initializing...  ");

  // Khởi tạo RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    lcd.setCursor(0, 2);
    lcd.print("  RTC ERROR!  ");
    while (1)
      delay(1000);
  }

  // Khởi tạo LoRa UART
  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  Serial.println("LoRa UART initialized");

  // Kết nối WiFi
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 2);
  lcd.print("Connecting WiFi...  ");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  lcd.setCursor(0, 2);
  lcd.print("WiFi: OK            ");
  delay(1000);

  // Đồng bộ thời gian từ NTP
  syncTimeFromNTP();

  // Kết nối MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  reconnectMQTT();

  lcd.clear();
  Serial.println("System Ready!");
}

void syncTimeFromNTP() {
  lcd.setCursor(0, 3);
  lcd.print("Syncing NTP...      ");

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (retry < 10) {
    rtc.adjust(DateTime(
      timeinfo.tm_year + 1900,
      timeinfo.tm_mon + 1,
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec
    ));
    Serial.println("\nRTC synced from NTP!");
    lcd.setCursor(0, 3);
    lcd.print("NTP: OK             ");
  } else {
    Serial.println("\nNTP sync failed! Using RTC.");
    lcd.setCursor(0, 3);
    lcd.print("NTP: FAILED         ");
  }
  delay(1000);
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting MQTT...");
    String clientId = "LORA2-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("Connected!");

      // Subscribe ALL topics
      client.subscribe(mqtt_topic_brightness);
      client.subscribe(mqtt_topic_schedule);
      client.subscribe(mqtt_topic_mode);
      client.subscribe(mqtt_topic_junction1);
      client.subscribe(mqtt_topic_junction2);
      client.subscribe(mqtt_topic_normal);
      client.subscribe(mqtt_topic_peak);
      client.subscribe(mqtt_topic_peak_schedule);

      Serial.println("Subscribed to all 8 topics");
    } else {
      Serial.print("Failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String topicStr = String(topic);
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("[MQTT] ");
  Serial.print(topicStr);
  Serial.print(": ");
  Serial.println(message);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, message);

  // Parse MQTT data LUÔN (bỏ điều kiện timeout để luôn cập nhật)
  if (topicStr == mqtt_topic_brightness && !error && doc.containsKey("value")) {
    brightnessValue = doc["value"];
    Serial.printf(">>> Brightness: %d%%\n", brightnessValue);
  } else if (topicStr == mqtt_topic_schedule && !error) {
    if (doc.containsKey("on"))
      timeOn = doc["on"].as<String>();
    if (doc.containsKey("off"))
      timeOff = doc["off"].as<String>();
    Serial.printf(">>> Schedule: %s - %s\n", timeOn.c_str(), timeOff.c_str());
  } else if (topicStr == mqtt_topic_mode) {
    if (message.indexOf("manual") >= 0)
      operationMode = "manual";
    else if (message.indexOf("auto") >= 0)
      operationMode = "auto";
    Serial.printf(">>> Mode: %s\n", operationMode.c_str());
  } else if (topicStr == mqtt_topic_junction1 && !error &&
             doc.containsKey("color")) {
    junction1Color = doc["color"].as<String>();
    Serial.printf(">>> J1: %s\n", junction1Color.c_str());
  } else if (topicStr == mqtt_topic_junction2 && !error &&
             doc.containsKey("color")) {
    junction2Color = doc["color"].as<String>();
    Serial.printf(">>> J2: %s\n", junction2Color.c_str());
  }
  // Auto mode topics
  else if (topicStr == mqtt_topic_normal && !error) {
    if (doc.containsKey("red"))
      normalRed = doc["red"];
    if (doc.containsKey("yellow"))
      normalYellow = doc["yellow"];
    if (doc.containsKey("green"))
      normalGreen = doc["green"];
    // Đồng bộ vào btn* để nút nhấ n tiếp theo bắt đầu từ giá trị web gửi
    btnRed    = normalRed;
    btnYellow = normalYellow;
    btnGreen  = normalGreen;
    Serial.printf(">>> Normal: R=%d Y=%d G=%d (synced to btn)\n", normalRed, normalYellow, normalGreen);
  } else if (topicStr == mqtt_topic_peak && !error) {
    if (doc.containsKey("red"))
      peakRed = doc["red"];
    if (doc.containsKey("yellow"))
      peakYellow = doc["yellow"];
    if (doc.containsKey("green"))
      peakGreen = doc["green"];
    Serial.printf(">>> Peak: R=%d Y=%d G=%d\n", peakRed, peakYellow, peakGreen);
  } else if (topicStr == mqtt_topic_peak_schedule && !error) {
    if (doc.containsKey("morning")) {
      JsonObject m = doc["morning"];
      if (m.containsKey("start"))
        mornStart = m["start"].as<String>();
      if (m.containsKey("end"))
        mornEnd = m["end"].as<String>();
    }
    if (doc.containsKey("afternoon")) {
      JsonObject a = doc["afternoon"];
      if (a.containsKey("start"))
        afterStart = a["start"].as<String>();
      if (a.containsKey("end"))
        afterEnd = a["end"].as<String>();
    }
    if (doc.containsKey("evening")) {
      JsonObject e = doc["evening"];
      if (e.containsKey("start"))
        evenStart = e["start"].as<String>();
      if (e.containsKey("end"))
        evenEnd = e["end"].as<String>();
    }
    Serial.println(">>> Peak schedule updated");
  }
}

void processLoRaData() {
  if (LoRaSerial.available()) {
    String loraData = LoRaSerial.readStringUntil('\n');
    loraData.trim();

    if (loraData.length() > 0) {
      lastLoRaReceive = millis(); // Cập nhật thời gian nhận LoRa

      Serial.print("[LoRa] ");
      Serial.println(loraData);

      // Parse LoRa data (format: BR:67, MODE:manual, J1:green, etc.)
      if (loraData.startsWith("BR:")) {
        brightnessValue = loraData.substring(3).toInt();
      } else if (loraData.startsWith("SCH:")) {
        // Format: SCH:18:00,06:00
        int commaPos = loraData.indexOf(',', 4);
        if (commaPos > 0) {
          timeOn = loraData.substring(4, commaPos);
          timeOff = loraData.substring(commaPos + 1);
        }
      } else if (loraData.startsWith("MODE:")) {
        operationMode = loraData.substring(5);
      } else if (loraData.startsWith("J1:")) {
        junction1Color = loraData.substring(3);
      } else if (loraData.startsWith("J2:")) {
        junction2Color = loraData.substring(3);
      }
    }
  }
}

bool isInSchedule(DateTime now) {
  // Kiểm tra xem hiện tại có trong khung giờ ON/OFF không
  if (timeOn.length() == 0 || timeOff.length() == 0)
    return false;

  int onHour = timeOn.substring(0, 2).toInt();
  int onMin = timeOn.substring(3, 5).toInt();
  int offHour = timeOff.substring(0, 2).toInt();
  int offMin = timeOff.substring(3, 5).toInt();

  int currentMinutes = now.hour() * 60 + now.minute();
  int onMinutes = onHour * 60 + onMin;
  int offMinutes = offHour * 60 + offMin;

  // Xử lý trường hợp qua 00:00 (ví dụ: 18:00 - 06:00)
  if (onMinutes > offMinutes) {
    return (currentMinutes >= onMinutes || currentMinutes < offMinutes);
  } else {
    return (currentMinutes >= onMinutes && currentMinutes < offMinutes);
  }
}

// Helper: in 2 chữ số lên LCD
void lcdPrint2d(int v) {
  if (v < 10) lcd.print("0");
  lcd.print(v);
}

void updateDisplay() {
  DateTime now = rtc.now();
  lcd.clear();

  // Dòng 1: Thời gian
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  lcdPrint2d(now.hour()); lcd.print(":");
  lcdPrint2d(now.minute()); lcd.print(":");
  lcdPrint2d(now.second());
  lcd.print(" ");
  lcdPrint2d(now.day()); lcd.print("/");
  lcdPrint2d(now.month());

  // Dòng 2: Mode
  lcd.setCursor(0, 1);
  lcd.print("Mode: ");
  lcd.print(operationMode);
  lcd.print("            ");

  if (inMenu) {
    // ===== ĐANG TRONG MENU =====
    lcd.setCursor(0, 2);

    if (operationMode == "manual") {
      switch (currentMenu) {
        case 0: // Toggle J1/J2
          lcd.print("[SET] J1:");
          lcd.print(junction1Color);
          lcd.print(" J2:");
          lcd.print(junction2Color);
          lcd.print("  ");
          break;
        case 1: // Street ON/OFF
          lcd.print("[SET] Street: ");
          lcd.print(manualStreetOn ? "ON " : "OFF");
          lcd.print("      ");
          break;
        case 2: // Brightness
          lcd.print("[SET] Bright:");
          lcd.print(brightnessValue);
          lcd.print("%    ");
          break;
      }
    } else { // auto
      switch (currentMenu) {
        case 0: // Street ON/OFF
          lcd.print("[SET] Street: ");
          lcd.print(manualStreetOn ? "ON " : "OFF");
          lcd.print("      ");
          break;
        case 1: // Brightness
          lcd.print("[SET] Bright:");
          lcd.print(brightnessValue);
          lcd.print("%    ");
          break;
        case 2: { // ON time
          int h = streetOnMinutes / 60, m = streetOnMinutes % 60;
          lcd.print("[SET] ON:");
          lcdPrint2d(h); lcd.print(":"); lcdPrint2d(m);
          lcd.print("          ");
          break;
        }
        case 3: { // OFF time
          int h = streetOffMinutes / 60, m = streetOffMinutes % 60;
          lcd.print("[SET] OFF:");
          lcdPrint2d(h); lcd.print(":"); lcdPrint2d(m);
          lcd.print("         ");
          break;
        }
        case 4: // Green
          lcd.print("[SET] Green:");
          lcd.print(btnGreen);
          lcd.print("s     ");
          break;
        case 5: // Red
          lcd.print("[SET] Red:  ");
          lcd.print(btnRed);
          lcd.print("s     ");
          break;
        case 6: // Yellow
          lcd.print("[SET] Yellow:");
          lcd.print(btnYellow);
          lcd.print("s   ");
          break;
      }
    }

    // Dòng 4: hướng dẫn
    lcd.setCursor(0, 3);
    lcd.print("UP+/DOWN-|MENU>>next");

  } else {
    // ===== MÀN HÌNH CHÍNH =====
    if (operationMode == "manual") {
      lcd.setCursor(0, 2);
      lcd.print("J1:"); lcd.print(junction1Color); lcd.print("          ");
      lcd.setCursor(0, 3);
      lcd.print("J2:"); lcd.print(junction2Color); lcd.print("          ");
    } else {
      lcd.setCursor(0, 2);
      if (manualStreetOn || isInSchedule(now)) {
        lcd.print("Light ON: "); lcd.print(brightnessValue); lcd.print("%     ");
      } else {
        lcd.print("Light: OFF         ");
      }
      lcd.setCursor(0, 3);
      lcd.print("Sched: ");
      lcd.print(timeOn); lcd.print("-"); lcd.print(timeOff);
      lcd.print("  ");
    }
  }
}

bool isInPeakHours(DateTime now) {
  int currentMin = now.hour() * 60 + now.minute();

  // Check morning peak
  if (mornStart.length() > 0 && mornEnd.length() > 0) {
    int mStart = mornStart.substring(0, 2).toInt() * 60 +
                 mornStart.substring(3, 5).toInt();
    int mEnd =
        mornEnd.substring(0, 2).toInt() * 60 + mornEnd.substring(3, 5).toInt();
    if (currentMin >= mStart && currentMin < mEnd)
      return true;
  }

  // Check afternoon peak
  if (afterStart.length() > 0 && afterEnd.length() > 0) {
    int aStart = afterStart.substring(0, 2).toInt() * 60 +
                 afterStart.substring(3, 5).toInt();
    int aEnd = afterEnd.substring(0, 2).toInt() * 60 +
               afterEnd.substring(3, 5).toInt();
    if (currentMin >= aStart && currentMin < aEnd)
      return true;
  }

  // Check evening peak
  if (evenStart.length() > 0 && evenEnd.length() > 0) {
    int eStart = evenStart.substring(0, 2).toInt() * 60 +
                 evenStart.substring(3, 5).toInt();
    int eEnd =
        evenEnd.substring(0, 2).toInt() * 60 + evenEnd.substring(3, 5).toInt();
    if (currentMin >= eStart && currentMin < eEnd)
      return true;
  }

  return false;
}

void sendArduinoCommands(DateTime now) {
  // Debug info
  Serial.printf(
      "[DEBUG] sendArduinoCommands called - Mode:%s, Time:%02d:%02d\n",
      operationMode.c_str(), now.hour(), now.minute());

  // 1. Điều khiển đèn đường
  // Ưu tiên nút nhấn thủ công, nếu không thì theo lịch tự động
  bool shouldBeOn = manualStreetOn || isInSchedule(now);
  Serial.printf("[DEBUG] Street light - ShouldBeOn:%d, Manual:%d, Brightness:%d%%\n",
                shouldBeOn, manualStreetOn, brightnessValue);
  if (shouldBeOn != lastStreetState || brightnessValue != lastStreetBright) {
    if (shouldBeOn) {
      ArduinoSerial.print("STREET:ON:");
      ArduinoSerial.println(brightnessValue);
      Serial.printf("[Arduino] STREET:ON:%d\n", brightnessValue);
    } else {
      ArduinoSerial.println("STREET:OFF");
      Serial.println("[Arduino] STREET:OFF");
    }
    lastStreetState = shouldBeOn;
    lastStreetBright = brightnessValue;
  }

  // 2. Điều khiển đèn giao thông
  if (operationMode == "manual") {
    // Manual mode - gửi trạng thái từng đèn
    if (junction1Color != lastSentJ1) {
      String j1Upper = junction1Color;
      j1Upper.toUpperCase();
      ArduinoSerial.print("MANUAL:J1:");
      ArduinoSerial.println(j1Upper);
      Serial.printf("[Arduino] MANUAL:J1:%s\n", j1Upper.c_str());
      lastSentJ1 = junction1Color;
    }

    if (junction2Color != lastSentJ2) {
      String j2Upper = junction2Color;
      j2Upper.toUpperCase();
      ArduinoSerial.print("MANUAL:J2:");
      ArduinoSerial.println(j2Upper);
      Serial.printf("[Arduino] MANUAL:J2:%s\n", j2Upper.c_str());
      lastSentJ2 = junction2Color;
    }

    lastSentMode = "manual";
  } else if (operationMode == "auto") {
    // Auto mode - gửi timing
    bool isPeak = isInPeakHours(now);

    if (isPeak) {
      if (lastSentMode != "peak" || peakRed != lastSentRed ||
          peakYellow != lastSentYellow || peakGreen != lastSentGreen) {
        ArduinoSerial.print("AUTO:PEAK:");
        ArduinoSerial.print(peakRed);
        ArduinoSerial.print(":");
        ArduinoSerial.print(peakYellow);
        ArduinoSerial.print(":");
        ArduinoSerial.println(peakGreen);
        Serial.printf("[Arduino] AUTO:PEAK:%d:%d:%d\n", peakRed, peakYellow,
                      peakGreen);

        lastSentMode = "peak";
        lastSentRed = peakRed;
        lastSentYellow = peakYellow;
        lastSentGreen = peakGreen;
      }
    } else {
      // Normal mode: dùng btnRed/btnGreen/btnYellow
      // (ai set sau thì thắng: web gửi thì sync vào btn*, nút nhấn thì chỉnh btn* trực tiếp)
      if (lastSentMode != "normal" || btnRed != lastSentRed ||
          btnYellow != lastSentYellow || btnGreen != lastSentGreen) {
        ArduinoSerial.print("AUTO:NORMAL:");
        ArduinoSerial.print(btnRed);
        ArduinoSerial.print(":");
        ArduinoSerial.print(btnYellow);
        ArduinoSerial.print(":");
        ArduinoSerial.println(btnGreen);
        Serial.printf("[Arduino] AUTO:NORMAL:%d:%d:%d\n", btnRed, btnYellow, btnGreen);

        lastSentMode   = "normal";
        lastSentRed    = btnRed;
        lastSentYellow = btnYellow;
        lastSentGreen  = btnGreen;
      }
    }
  }
}

void checkTimeSetCommand() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.startsWith("SET,")) {
      int y, m, d, h, mi, s;
      if (sscanf(input.c_str(), "SET,%d,%d,%d,%d,%d,%d", &y, &m, &d, &h, &mi,
                 &s) == 6) {
        rtc.adjust(DateTime(y, m, d, h, mi, s));
        Serial.println("Time updated via Serial!");
        updateDisplay();
      } else {
        Serial.println("Invalid format. Use: SET,YYYY,MM,DD,HH,MM,SS");
      }
    }
  }
}

void loop() {
  // Duy trì kết nối MQTT
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  // Nhận data từ LoRa
  processLoRaData();
  
  // Xử lý nút nhấn
  handleButtons();  // <--- THÊM DÒNG NÀY

  // Kiểm tra lệnh cài đặt thời gian qua Serial
  checkTimeSetCommand();

  // Cập nhật màn hình và gửi lệnh Arduino
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    DateTime now = rtc.now();
    updateDisplay();
    sendArduinoCommands(now);
  }

  delay(10);
}

// Helper: ghi lại timeOn/timeOff từ phút
void applyStreetSchedule() {
  char buf[6];
  sprintf(buf, "%02d:%02d", streetOnMinutes / 60, streetOnMinutes % 60);
  timeOn = String(buf);
  sprintf(buf, "%02d:%02d", streetOffMinutes / 60, streetOffMinutes % 60);
  timeOff = String(buf);
}

// Helper: gửi timing đèn giao thông xuống Arduino
void sendAutoTiming() {
  ArduinoSerial.print("AUTO:NORMAL:");
  ArduinoSerial.print(btnRed);
  ArduinoSerial.print(":");
  ArduinoSerial.print(btnYellow);
  ArduinoSerial.print(":");
  ArduinoSerial.println(btnGreen);
  Serial.printf("[BTN] AUTO timing R=%d Y=%d G=%d\n", btnRed, btnYellow, btnGreen);
  // reset lastSent để force gửi lại
  lastSentMode = "";
}

void handleButtons() {
  unsigned long currentTime = millis();

  // Tự thoát menu sau menuTimeoutMs giây không nhấn
  if (inMenu && (currentTime - menuLastAction > menuTimeoutMs)) {
    inMenu = false;
    updateDisplay();
  }

  // ── Nút BTN_MODE (GPIO 4): Chuyển mode Manual ↔ Auto ──
  if (digitalRead(BTN_MODE) == LOW && (currentTime - lastBtnPress[0] > debounceDelay)) {
    lastBtnPress[0] = currentTime;
    
    if (operationMode == "manual") {
      operationMode = "auto";n
      Serial.println("[BTN] Mode -> AUTO");
      sendAutoTiming();
    } else {
      operationMode = "manual";
      Serial.println("[BTN] Mode -> MANUAL");
      junction1Color = "red";
      junction2Color = "green";
      ArduinoSerial.println("MANUAL:J1:RED");
      ArduinoSerial.println("MANUAL:J2:GREEN");
      lastSentJ1 = junction1Color;
      lastSentJ2 = junction2Color;
    }
    inMenu = false; // thoát menu khi đổi mode
    updateDisplay();
  }

  // ── Nút BTN_MENU (GPIO 5): Vào menu / chuyển menu tiếp theo ──
  if (digitalRead(BTN_MENU) == LOW && (currentTime - lastBtnPress[1] > debounceDelay)) {
    lastBtnPress[1] = currentTime;
    menuLastAction = currentTime;
    int maxMenu = (operationMode == "manual") ? MENU_MANUAL_COUNT : MENU_AUTO_COUNT;
    if (!inMenu) {
      inMenu = true;
      currentMenu = 0;
    } else {
      currentMenu = (currentMenu + 1) % maxMenu;
    }
    Serial.printf("[BTN] Menu -> %d\n", currentMenu);
    updateDisplay();
  }

  // ── Nút BTN_UP (GPIO 18): Tăng giá trị / hành động UP ──
  if (digitalRead(BTN_UP) == LOW && (currentTime - lastBtnPress[2] > debounceDelay)) {
    lastBtnPress[2] = currentTime;
    menuLastAction = currentTime;

    if (!inMenu) {
      // Ngoài menu: bật đèn đường thủ công
      if (brightnessValue == 0) brightnessValue = 70;
      manualStreetOn = true;
      ArduinoSerial.print("STREET:ON:"); ArduinoSerial.println(brightnessValue);
      lastStreetState = true; lastStreetBright = brightnessValue;
      Serial.printf("[BTN] Street ON at %d%%\n", brightnessValue);
    } else {
      // Trong menu: tăng giá trị
      if (operationMode == "manual") {
        switch (currentMenu) {
          case 0: // Toggle J1/J2
            if (junction1Color == "red") { junction1Color = "green"; junction2Color = "red"; }
            else { junction1Color = "red"; junction2Color = "green"; }
            { String j1u = junction1Color; j1u.toUpperCase();
              String j2u = junction2Color; j2u.toUpperCase();
              ArduinoSerial.print("MANUAL:J1:"); ArduinoSerial.println(j1u);
              ArduinoSerial.print("MANUAL:J2:"); ArduinoSerial.println(j2u);
              lastSentJ1 = junction1Color; lastSentJ2 = junction2Color; }
            break;
          case 1: // Street ON/OFF
            manualStreetOn = true;
            if (brightnessValue == 0) brightnessValue = 70;
            ArduinoSerial.print("STREET:ON:"); ArduinoSerial.println(brightnessValue);
            lastStreetState = true; lastStreetBright = brightnessValue;
            break;
          case 2: // Brightness +5
            brightnessValue = min(100, brightnessValue + 5);
            if (manualStreetOn) { ArduinoSerial.print("STREET:ON:"); ArduinoSerial.println(brightnessValue); }
            break;
        }
      } else { // auto
        switch (currentMenu) {
          case 0: // Street ON
            manualStreetOn = true;
            if (brightnessValue == 0) brightnessValue = 70;
            ArduinoSerial.print("STREET:ON:"); ArduinoSerial.println(brightnessValue);
            lastStreetState = true; lastStreetBright = brightnessValue;
            break;
          case 1: // Brightness +5
            brightnessValue = min(100, brightnessValue + 5);
            if (manualStreetOn) { ArduinoSerial.print("STREET:ON:"); ArduinoSerial.println(brightnessValue); }
            break;
          case 2: // ON time +30min
            streetOnMinutes = (streetOnMinutes + 30) % (24 * 60);
            applyStreetSchedule();
            break;
          case 3: // OFF time +30min
            streetOffMinutes = (streetOffMinutes + 30) % (24 * 60);
            applyStreetSchedule();
            break;
          case 4: // Green +1s
            btnGreen = min(99, btnGreen + 1);
            sendAutoTiming();
            break;
          case 5: // Red +1s
            btnRed = min(99, btnRed + 1);
            sendAutoTiming();
            break;
          case 6: // Yellow +1s
            btnYellow = min(9, btnYellow + 1);
            sendAutoTiming();
            break;
        }
      }
    }
    updateDisplay();
  }

  // ── Nút BTN_DOWN (GPIO 19): Giảm giá trị / hành động DOWN ──
  if (digitalRead(BTN_DOWN) == LOW && (currentTime - lastBtnPress[3] > debounceDelay)) {
    lastBtnPress[3] = currentTime;
    menuLastAction = currentTime;

    if (!inMenu) {
      // Ngoài menu: tắt đèn đường thủ công
      manualStreetOn = false;
      ArduinoSerial.println("STREET:OFF");
      lastStreetState = false;
      Serial.println("[BTN] Street OFF");
    } else {
      // Trong menu: giảm giá trị
      if (operationMode == "manual") {
        switch (currentMenu) {
          case 0: // Toggle J1/J2 (UP và DOWN đều toggle)
            if (junction1Color == "red") { junction1Color = "green"; junction2Color = "red"; }
            else { junction1Color = "red"; junction2Color = "green"; }
            { String j1u = junction1Color; j1u.toUpperCase();
              String j2u = junction2Color; j2u.toUpperCase();
              ArduinoSerial.print("MANUAL:J1:"); ArduinoSerial.println(j1u);
              ArduinoSerial.print("MANUAL:J2:"); ArduinoSerial.println(j2u);
              lastSentJ1 = junction1Color; lastSentJ2 = junction2Color; }
            break;
          case 1: // Street OFF
            manualStreetOn = false;
            ArduinoSerial.println("STREET:OFF");
            lastStreetState = false;
            break;
          case 2: // Brightness -5
            brightnessValue = max(5, brightnessValue - 5);
            if (manualStreetOn) { ArduinoSerial.print("STREET:ON:"); ArduinoSerial.println(brightnessValue); }
            break;
        }
      } else { // auto
        switch (currentMenu) {
          case 0: // Street OFF
            manualStreetOn = false;
            ArduinoSerial.println("STREET:OFF");
            lastStreetState = false;
            break;
          case 1: // Brightness -5
            brightnessValue = max(5, brightnessValue - 5);
            if (manualStreetOn) { ArduinoSerial.print("STREET:ON:"); ArduinoSerial.println(brightnessValue); }
            break;
          case 2: // ON time -30min
            streetOnMinutes = (streetOnMinutes - 30 + 24 * 60) % (24 * 60);
            applyStreetSchedule();
            break;
          case 3: // OFF time -30min
            streetOffMinutes = (streetOffMinutes - 30 + 24 * 60) % (24 * 60);
            applyStreetSchedule();
            break;
          case 4: // Green -1s
            btnGreen = max(1, btnGreen - 1);
            sendAutoTiming();
            break;
          case 5: // Red -1s
            btnRed = max(1, btnRed - 1);
            sendAutoTiming();
            break;
          case 6: // Yellow -1s
            btnYellow = max(1, btnYellow - 1);
            sendAutoTiming();
            break;
        }
      }
    }
    updateDisplay();
  }
}
