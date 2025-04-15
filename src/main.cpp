#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define PPG_WIDTH 65     // PPG waveform width, in pixels
#define PACKET_SIZE 76   // Packet size, in bytes
#define PPG_MIN -128     // PPG waveform min value
#define PPG_MAX 127      // PPG waveform max value
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define USART_RX_BUF_SIZE 128
uint8_t USART_RX_BUF[USART_RX_BUF_SIZE];

static int displayCount = 0;

/**
 * @brief display waveform data and blood pressure data
 * @note
 *      bit 2-65: Waveform data
 *      bit 72: Systolic blood pressure
 * 		bit 73: Diastolic blood pressure
 *	    bit 74: Peripheral resistance
 */
void displaydata() {
  displayCount++;
  display.clearDisplay();

  /* PPG waveform data */
  if (displayCount >= 5) {
    for (int i = 1; i < PPG_WIDTH; i++) {
      int x = map(i - 1, 0, PPG_WIDTH - 1, 0, SCREEN_WIDTH / 2 - 1);

      int value = (int8_t)USART_RX_BUF[i];
      int y = map(value, PPG_MIN, PPG_MAX, SCREEN_HEIGHT - 1, 0);
      display.drawPixel(x, y, SSD1306_WHITE);
    }
    displayCount = 0;
  }

  /* Blood pressure data */
  int systolic = USART_RX_BUF[71];
  int diastolic = USART_RX_BUF[72];
  int resistance = USART_RX_BUF[73];

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(SCREEN_WIDTH / 2, 0);
  display.print("Sys: ");
  display.print(systolic);

  display.setCursor(SCREEN_WIDTH / 2, 10);
  display.print("Dia: ");
  display.print(diastolic);

  display.setCursor(SCREEN_WIDTH / 2, 20);
  display.print("Res: ");
  display.print(resistance);

  display.display();
}

void setup() {

  /* Initialize serial; MKS-141 :38400; Send 0x8a to start*/
  Serial.begin(38400);
  Serial.write(0x8a);

  /* Initialize OLED*/
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.display();
}

void loop() {
  static int dataCount = 0;

  if (Serial.available() > 0) {
    int incomingByte = Serial.read();

    /* Frame Header: 0xFF*/
    if (dataCount == 0 && incomingByte != 0xFF) {
      return;
    }

    if (dataCount < PACKET_SIZE) {
      USART_RX_BUF[dataCount++] = incomingByte;

      if (dataCount == PACKET_SIZE) {
        displaydata();
        dataCount = 0;
        memset(USART_RX_BUF, 0, sizeof(USART_RX_BUF));
      }
    }
  }
}