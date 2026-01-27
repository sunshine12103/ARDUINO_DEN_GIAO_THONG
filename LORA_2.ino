/*
 * ESP32 LORA_2 - Slave/Display Node
 * - RTC DS1307: Hiển thị thời gian
 * - LoRa UART (RX=16, TX=17): Nhận data từ Master (primary)
 * - MQTT: Backup khi LoRa lỗi
 * - LCD 20x4: Hiển thị thời gian, mode, brightness, junction status
 */

#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include <RTClib.h>
#include <WiFi.h>
#include <Wire.h>

// Khởi tạo RTC
RTC_DS1307 rtc;

// Khởi tạo LCD I2C 20x4
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Khởi tạo LoRa UART
HardwareSerial LoRaSerial(2);
#define LORA_RX 16
#define LORA_TX 17

// WiFi credentials
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

// Biến đánh dấu nguồn dữ liệu
unsigned long lastLoRaReceive = 0;
unsigned long loraTimeout = 10000; // 10s không nhận LoRa → chuyển MQTT

// UART với Arduino
HardwareSerial ArduinoSerial(1); // Serial1
#define ARDUINO_TX 33
#define ARDUINO_RX 25

void setup() {
  // Serial cho debug
  Serial.begin(9600);
  delay(1000);
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

  // Kết nối MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  reconnectMQTT();

  lcd.clear();
  Serial.println("System Ready!");
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
    Serial.printf(">>> Normal: R=%d Y=%d G=%d\n", normalRed, normalYellow,
                  normalGreen);
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

void updateDisplay() {
  DateTime now = rtc.now();
  lcd.clear();

  // Dòng 1: Thời gian
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  if (now.hour() < 10)
    lcd.print("0");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10)
    lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10)
    lcd.print("0");
  lcd.print(now.second());
  lcd.print(" ");
  if (now.day() < 10)
    lcd.print("0");
  lcd.print(now.day());
  lcd.print("/");
  if (now.month() < 10)
    lcd.print("0");
  lcd.print(now.month());

  // Dòng 2: Mode
  lcd.setCursor(0, 1);
  lcd.print("Mode: ");
  lcd.print(operationMode);
  lcd.print("              ");

  // Dòng 3 & 4: Tùy mode
  if (operationMode == "manual") {
    // Hiển thị trạng thái 2 đèn giao lộ
    lcd.setCursor(0, 2);
    lcd.print("J1: ");
    lcd.print(junction1Color);
    lcd.print("          ");

    lcd.setCursor(0, 3);
    lcd.print("J2: ");
    lcd.print(junction2Color);
    lcd.print("          ");
  } else {
    // Hiển thị độ sáng đèn đường (nếu trong khung giờ)
    lcd.setCursor(0, 2);
    if (isInSchedule(now)) {
      lcd.print("Light ON: ");
      lcd.print(brightnessValue);
      lcd.print("%     ");
    } else {
      lcd.print("Light: OFF         ");
    }

    lcd.setCursor(0, 3);
    lcd.print("Sched: ");
    lcd.print(timeOn);
    lcd.print("-");
    lcd.print(timeOff);
    lcd.print("  ");
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
  bool shouldBeOn = isInSchedule(now);
  Serial.printf("[DEBUG] Street light - ShouldBeOn:%d, Brightness:%d%%\n",
                shouldBeOn, brightnessValue);
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
      if (lastSentMode != "normal" || normalRed != lastSentRed ||
          normalYellow != lastSentYellow || normalGreen != lastSentGreen) {
        ArduinoSerial.print("AUTO:NORMAL:");
        ArduinoSerial.print(normalRed);
        ArduinoSerial.print(":");
        ArduinoSerial.print(normalYellow);
        ArduinoSerial.print(":");
        ArduinoSerial.println(normalGreen);
        Serial.printf("[Arduino] AUTO:NORMAL:%d:%d:%d\n", normalRed,
                      normalYellow, normalGreen);

        lastSentMode = "normal";
        lastSentRed = normalRed;
        lastSentYellow = normalYellow;
        lastSentGreen = normalGreen;
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
