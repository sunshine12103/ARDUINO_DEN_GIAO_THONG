#include <Arduino.h>
#include <ShiftRegister74HC595.h>

#define LIGHT1_PIN A0
#define LIGHT2_PIN A1
#define LIGHT3_PIN A2
#define LIGHT4_PIN A3

// Create shift register object with 2 registers
ShiftRegister74HC595<2> sr(8, 9, 10);

int number = 99; // <--- Change it from 0 to 99

int value, digit1, digit2, digit3, digit4;
uint8_t numberB[] = {
    B11000000, //0
    B11111001, //1
    B10100100, //2
    B10110000, //3
    B10011001, //4
    B10010010, //5
    B10000010, //6 - Đã sửa để hiển thị đủ 6 nét
    B11111000, //7
    B10000000, //8
    B10010000  //9 - Đã sửa để hiển thị đủ 6 nét
};

// Function declarations
void countUp();
void countDown();
void blink();


void setup() {
  Serial.begin(115200);
  pinMode(LIGHT1_PIN, OUTPUT);
  pinMode(LIGHT2_PIN, OUTPUT);
  pinMode(LIGHT3_PIN, OUTPUT);
  pinMode(LIGHT4_PIN, OUTPUT);

  // Không gọi các hàm LED 7 đoạn
  // countUp();
  // countDown();
  // blink();
}

void loop() {
  digitalWrite(LIGHT1_PIN, HIGH);
  digitalWrite(LIGHT2_PIN, HIGH);
  digitalWrite(LIGHT3_PIN, HIGH);
  digitalWrite(LIGHT4_PIN, HIGH);
  delay(3000);
  digitalWrite(LIGHT1_PIN, LOW);
  digitalWrite(LIGHT2_PIN, LOW);
  digitalWrite(LIGHT3_PIN, LOW);
  digitalWrite(LIGHT4_PIN, LOW);
  delay(1000);

}

void countUp()
{
    for (int i = 0; i <= number; i++)
    {
        //Split number to digits:
        digit2 = i % 10;
        digit1 = (i / 10) % 10;
        //Send them to 7 segment displays
        uint8_t numberToPrint[] = {numberB[digit1], numberB[digit2]};
        sr.setAll(numberToPrint);
        //Reset them for next time
        digit1 = 0;
        digit2 = 0;
        delay(1000); // Repeat every 1 sec
    }
}

void countDown()
{
    for (number; number >= 0; number--)
    {
        //Split number to digits:
        digit2 = number % 10;
        digit1 = (number / 10) % 10;
        //Send them to 7 segment displays
        uint8_t numberToPrint[] = {numberB[digit1], numberB[digit2]};
        sr.setAll(numberToPrint);
        //Reset them for next time
        digit1 = 0;
        digit2 = 0;
        delay(1000); // Repeat every 1 sec
    }
}

//Blink
void blink()
{
    for (int i = 0; i < 4; i++)
    {
        sr.setAllLow(); // set all pins Low (off)
        delay(1000);
        sr.setAllHigh(); // set all pins High (on)
        delay(1000);
    }
}
