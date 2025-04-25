#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

const int analogInPin = A0;
#define PPG_DATA_SIZE 128
int ppgData[PPG_DATA_SIZE];
int ppgIndex = 0;

#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float filteredData[PPG_DATA_SIZE];

#define MIN_PROCESS_INTERVAL 10 //  Minimum processing interval (ms)
#define MIN_DISPLAY_INTERVAL 30 // Minimum display refresh interval (ms)

// Buffer to store multiple periods of data
#define DISPLAY_BUFFER_SIZE 128
float displayBuffer[DISPLAY_BUFFER_SIZE];

#define FILTER_WINDOW 9 // Adjust filter window size, less than PPG period
#define HALF_WINDOW (FILTER_WINDOW / 2)

// Store raw and filtered data separately
float rawDisplayBuffer[DISPLAY_BUFFER_SIZE];
float filteredDisplayBuffer[DISPLAY_BUFFER_SIZE];

// Moving average filter with triangle window
void applyMovingAverage() {
  float sum = 0;
  float weightSum = 0;

  // Calculate index of latest point
  int currentIndex = (ppgIndex == 0) ? PPG_DATA_SIZE - 1 : ppgIndex - 1;

  // Use triangle window weights
  for (int j = -HALF_WINDOW; j <= HALF_WINDOW; j++) {
    // Calculate weight: maximum at center
    float weight = HALF_WINDOW - abs(j);

    // Access data using circular buffer
    int index = (currentIndex + j + PPG_DATA_SIZE) % PPG_DATA_SIZE;

    sum += ppgData[index] * weight;
    weightSum += weight;
  }

  // Calculate weighted average
  filteredData[currentIndex] = sum / weightSum;
}

/**
 * @brief Read and print raw PPG data from ADC
 */
void readAndPrintPPGData() {
  int adcValue = analogRead(analogInPin); // Read and invert ADC data
  adcValue = 1023 - adcValue;             // Invert raw data

  // Update moving average
  static float smoothedValue = adcValue;
  const float ALPHA = 0.6;
  smoothedValue = ALPHA * adcValue + (1 - ALPHA) * smoothedValue;

  // Write to circular buffer
  ppgData[ppgIndex] = smoothedValue;
  ppgIndex = (ppgIndex + 1) % PPG_DATA_SIZE;
}

// Update display buffer
void updateDisplayBuffer() {
  // Update raw data buffer
  for (int i = DISPLAY_BUFFER_SIZE - 1; i > 0; i--) {
    rawDisplayBuffer[i] = rawDisplayBuffer[i - 1];
    filteredDisplayBuffer[i] = filteredDisplayBuffer[i - 1];
  }

  // Insert latest data point
  int latestIndex = (ppgIndex == 0) ? PPG_DATA_SIZE - 1 : ppgIndex - 1;
  rawDisplayBuffer[0] = ppgData[latestIndex];
  filteredDisplayBuffer[0] = filteredData[latestIndex];
}

void displayPPGWaveform() {
  display.clearDisplay();

  // Calculate ranges for raw and filtered data
  float rawMin = 999, rawMax = -999;
  float filteredMin = 999, filteredMax = -999;

  // Find ranges for both datasets
  for (int i = 0; i < DISPLAY_BUFFER_SIZE; i++) {
    if (rawDisplayBuffer[i] < rawMin)
      rawMin = rawDisplayBuffer[i];
    if (rawDisplayBuffer[i] > rawMax)
      rawMax = rawDisplayBuffer[i];
    if (filteredDisplayBuffer[i] < filteredMin)
      filteredMin = filteredDisplayBuffer[i];
    if (filteredDisplayBuffer[i] > filteredMax)
      filteredMax = filteredDisplayBuffer[i];
  }

  // Add boundary protection
  if (rawMax == rawMin)
    rawMax = rawMin + 1;
  if (filteredMax == filteredMin)
    filteredMax = filteredMin + 1;

  // Draw raw waveform (dotted line) - in upper half (0-31 pixels)
  float lastY = -1;
  int lastX = -1;
  for (int i = 0; i < DISPLAY_BUFFER_SIZE; i++) {
    int x = map(i, 0, DISPLAY_BUFFER_SIZE - 1, 0, SCREEN_WIDTH / 2 - 1);
    // Map to upper half
    int y = map(rawDisplayBuffer[i], rawMin, rawMax, 31, 2);
    if (lastY >= 0 && y >= 0) {
      if (i % 2 == 0) { // Dotted line effect
        display.drawLine(lastX, lastY, x, y, SSD1306_WHITE);
      }
    }
    lastX = x;
    lastY = y;
  }

  // Draw filtered waveform (solid line) - in lower half (33-63 pixels)
  lastY = -1;
  lastX = -1;
  for (int i = 0; i < DISPLAY_BUFFER_SIZE; i++) {
    int x = map(i, 0, DISPLAY_BUFFER_SIZE - 1, 0, SCREEN_WIDTH / 2 - 1);
    // Map to lower half
    int y = map(filteredDisplayBuffer[i], filteredMin, filteredMax, 63, 34);
    if (lastY >= 0 && y >= 0) {
      display.drawLine(lastX, lastY, x, y, SSD1306_WHITE);
    }
    lastX = x;
    lastY = y;
  }

  // Draw horizontal and vertical dividing lines
  display.drawFastHLine(0, 32, SCREEN_WIDTH / 2,
                        SSD1306_WHITE); // Horizontal divider
  display.drawFastVLine(SCREEN_WIDTH / 2, 0, SCREEN_HEIGHT,
                        SSD1306_WHITE); // Vertical divider

  display.display();
}

void setup() {
  Serial.begin(115200);

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
  static unsigned long lastSampleTime = 0;
  static unsigned long lastDisplayTime = 0;
  unsigned long currentTime = millis();

  // 10ms
  if (currentTime - lastSampleTime >= MIN_PROCESS_INTERVAL) {
    readAndPrintPPGData();
    applyMovingAverage();
    lastSampleTime = currentTime;
  }

  // 30ms
  if (currentTime - lastDisplayTime >= MIN_DISPLAY_INTERVAL) {
    updateDisplayBuffer();
    displayPPGWaveform();
    lastDisplayTime = currentTime;
  }
}