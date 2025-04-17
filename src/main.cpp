#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#define MKS_141 0
#define PPG_Origin 1

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#ifdef MKS_141
#define PPG_WIDTH 65   // PPG waveform width, in pixels
#define PACKET_SIZE 76 // Packet size, in bytes
#define PPG_MIN -128   // PPG waveform min value
#define PPG_MAX 127    // PPG waveform max value
static int displayCount = 0;
static int dataCount = 0;
#endif /* MKS_141 */

#ifdef PPG_Origin
const int analogInPin = A0;
#define PPG_DATA_SIZE 128
int ppgData[PPG_DATA_SIZE];
int ppgIndex = 0;
#endif /* PPG_Origin */

#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define USART_RX_BUF_SIZE 128
uint8_t USART_RX_BUF[USART_RX_BUF_SIZE];

/**
 * @brief Read and print raw PPG data from ADC
 */
void readAndPrintPPGData() {
  int adcValue = analogRead(analogInPin); // Read ADC value
  Serial.print("Raw PPG Data: ");
  Serial.println(adcValue); // Print ADC value to serial terminal
  ppgData[ppgIndex] = adcValue;
  ppgIndex = (ppgIndex + 1) % PPG_DATA_SIZE;
}

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
void displayPPGWaveform() {
  display.clearDisplay();

  for (int i = 1; i < PPG_DATA_SIZE; i++) {

    int x1 = map(i - 1, 0, PPG_DATA_SIZE - 1, 0, SCREEN_WIDTH * 2 - 1);
    int y1 = map(ppgData[i - 1], 0, 1023, SCREEN_HEIGHT - 1, 0);
    int x2 = map(i, 0, PPG_DATA_SIZE - 1, 0, SCREEN_WIDTH * 2 - 1);
    int y2 = map(ppgData[i], 0, 1023, SCREEN_HEIGHT - 1, 0);

    if (x1 < SCREEN_WIDTH && x2 < SCREEN_WIDTH) {
      display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
    }
  }

  display.display();
}

void setup() {

#ifdef MKS_141
  /* Initialize serial; MKS-141 :38400; Send 0x8a to start*/
  Serial.begin(38400);
  Serial.write(0x8a);
#endif /* MKS_141 */

#ifdef PPG_Origin
  /* Initialize serial; PPG_Origin :115200*/
  Serial.begin(115200);
#endif /* PPG_Origin */

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

#ifdef MKS_141
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

#endif /* MKS_141 */

#ifdef PPG_Origin
  /* Read and print raw PPG data */
  readAndPrintPPGData();
  // displayPPGWaveform();
  delay(50);
#endif /* PPG_Origin */
}