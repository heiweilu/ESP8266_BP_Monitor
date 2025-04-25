#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#define DEBUG 1
#define SCREEN_WIDTH 128         // OLED display width, in pixels
#define SCREEN_HEIGHT 64         // OLED display height, in pixels
#define MIN_PROCESS_INTERVAL 10  //  Minimum processing interval (ms)
#define MIN_DISPLAY_INTERVAL 100 // Minimum display refresh interval (ms)
#define MIN_DETECT_INTERVAL 100
#define MIN_UPDATE_INTERVAL 500
#define PPG_DATA_SIZE 64
#define DISPLAY_BUFFER_SIZE 64
#define FILTER_WINDOW 9 // Adjust filter window size, less than PPG period
#define HALF_WINDOW (FILTER_WINDOW / 2)
#define HISTORY_SIZE 10
#define MAX_PEAKS 5

const int analogInPin = A0;

int ppgData[PPG_DATA_SIZE];
int ppgIndex = 0;

#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float filteredData[PPG_DATA_SIZE];

/* Buffer to store multiple periods of data */
float displayBuffer[DISPLAY_BUFFER_SIZE];

/* Store raw and filtered data separately */
float rawDisplayBuffer[DISPLAY_BUFFER_SIZE];
float filteredDisplayBuffer[DISPLAY_BUFFER_SIZE];

struct AdaptiveParams {
  float minABInterval;
  float maxABInterval;
  float minBCInterval;
  float maxBCInterval;
  float minCDInterval;
  float maxCDInterval;
  float minValidAmp;
  float pulseFreq;
  float avgPulsePeriod;
} params;

struct FeatureHistory {
  float abIntervals[HISTORY_SIZE];
  float bcIntervals[HISTORY_SIZE];
  float cdIntervals[HISTORY_SIZE];
  float amplitudes[HISTORY_SIZE];
  int index;
} history;

int validPeakCount = 0;
float apgData[PPG_DATA_SIZE];
int validAPoints[MAX_PEAKS], validBPoints[MAX_PEAKS], validCPoints[MAX_PEAKS],
    validDPoints[MAX_PEAKS];

void updateAdaptiveParams() {
  float totalPeriod = 0;
  int validCount = 0;
  for (int i = 1; i < validPeakCount; i++) {
    int period = validAPoints[i] - validAPoints[i - 1];
    if (period > 0) {
      totalPeriod += period;
      validCount++;
    }
  }

  if (validCount > 0) {
    params.avgPulsePeriod = totalPeriod / validCount;
    params.pulseFreq = 1000.0 / (params.avgPulsePeriod * MIN_PROCESS_INTERVAL);

    params.minABInterval = params.avgPulsePeriod * 0.15; // 15% of period
    params.maxABInterval = params.avgPulsePeriod * 0.35; // 35% of period
    params.minBCInterval = params.avgPulsePeriod * 0.20; // 20% of period
    params.maxBCInterval = params.avgPulsePeriod * 0.40; // 40% of period
    params.minCDInterval = params.avgPulsePeriod * 0.10; // 10% of period
    params.maxCDInterval = params.avgPulsePeriod * 0.25; // 25% of period

    float maxAmp = -99999, minAmp = 99999;
    for (int i = 0; i < PPG_DATA_SIZE; i++) {
      if (filteredData[i] > maxAmp)
        maxAmp = filteredData[i];
      if (filteredData[i] < minAmp)
        minAmp = filteredData[i];
    }
    params.minValidAmp = (maxAmp - minAmp) * 0.1; // 10% of signal range
  }
}

// Calculate second-order difference (APG signal) with normalization
void calculateAPG() {
  // Calculate second-order difference
  for (int i = 2; i < PPG_DATA_SIZE; i++) {
    apgData[i] =
        filteredData[i] - 2 * filteredData[i - 1] + filteredData[i - 2];
  }

  // Normalize APG signal
  float maxAPG = -99999, minAPG = 99999;
  for (int i = 2; i < PPG_DATA_SIZE; i++) {
    if (apgData[i] > maxAPG)
      maxAPG = apgData[i];
    if (apgData[i] < minAPG)
      minAPG = apgData[i];
  }

  if (maxAPG != minAPG) {
    for (int i = 2; i < PPG_DATA_SIZE; i++) {
      apgData[i] = (apgData[i] - minAPG) / (maxAPG - minAPG);
    }
  }
}

// Feature point detection
void detectFeatures() {
  validPeakCount = 0;
  float maxVal = -99999, minVal = 99999;

  // Find data range and calculate average value
  float sum = 0;
  for (int i = 0; i < PPG_DATA_SIZE; i++) {
    if (filteredData[i] > maxVal)
      maxVal = filteredData[i];
    if (filteredData[i] < minVal)
      minVal = filteredData[i];
    sum += filteredData[i];
  }
  float avgVal = sum / PPG_DATA_SIZE;

  // Dynamic threshold setting
  float valleyThreshold = minVal + (avgVal - minVal) * 0.4;
  float peakThreshold = avgVal + (maxVal - avgVal) * 0.3;

  // A point detection (valley)
  int lastA = -params.avgPulsePeriod;
  for (int i = 5; i < PPG_DATA_SIZE - 5; i++) {
    if (filteredData[i] < valleyThreshold &&
        (i - lastA) >= params.avgPulsePeriod * 0.7) {

      // APG signal validation - A point should correspond to APG zero-crossing
      // and positive slope
      bool isValidAPG = (i >= 2) && (apgData[i] < 0.2) && // APG value is small
                        (apgData[i + 1] > apgData[i]);    // Positive slope

      // Local minimum value validation
      bool isValley = true;
      int windowSize = 5;
      for (int j = 1; j <= windowSize; j++) {
        if (i - j >= 0 && i + j < PPG_DATA_SIZE) {
          if (filteredData[i] > filteredData[i - j] ||
              filteredData[i] > filteredData[i + j]) {
            isValley = false;
            break;
          }
        }
      }

      if (isValley && isValidAPG && validPeakCount < MAX_PEAKS) {
        validAPoints[validPeakCount] = i;
        lastA = i;
        validPeakCount++;
      }
    }
  }

  // Based on A point detection other feature points
  for (int i = 0; i < validPeakCount; i++) {
    int windowStart = validAPoints[i];
    int windowEnd =
        (i < validPeakCount - 1) ? validAPoints[i + 1] : PPG_DATA_SIZE - 5;

    // B point detection (main peak)
    float maxB = -99999;
    validBPoints[i] = windowStart + (int)params.minABInterval;
    for (int j = windowStart + (int)params.minABInterval;
         j < (int)(windowStart + params.maxABInterval) && j < windowEnd; j++) {

      // APG signal validation - B point should correspond to APG positive peak
      bool isValidBAPG =
          (j >= 2) && (apgData[j] > 0.6) && // APG strong positive value
          (apgData[j] > apgData[j - 1]) && (apgData[j] > apgData[j + 1]);

      if (filteredData[j] > maxB && filteredData[j] >= filteredData[j - 1] &&
          filteredData[j] >= filteredData[j + 1] && isValidBAPG) {
        maxB = filteredData[j];
        validBPoints[i] = j;
      }
    }

    // C point detection (secondary valley)
    float minC = 99999;
    validCPoints[i] = validBPoints[i] + (int)params.minBCInterval;
    for (int j = validBPoints[i] + (int)params.minBCInterval;
         j < (int)(validBPoints[i] + params.maxBCInterval) && j < windowEnd;
         j++) {

      // APG signal validation - C point should correspond to APG negative peak
      bool isValidCAPG =
          (j >= 2) && (apgData[j] < -0.3) && // APG strong negative value
          (apgData[j] < apgData[j - 1]) && (apgData[j] < apgData[j + 1]);

      if (filteredData[j] < minC && filteredData[j] <= filteredData[j - 1] &&
          filteredData[j] <= filteredData[j + 1] && isValidCAPG) {
        minC = filteredData[j];
        validCPoints[i] = j;
      }
    }

    // D point detection (secondary peak)
    float maxD = -99999;
    validDPoints[i] = validCPoints[i] + (int)params.minCDInterval;
    for (int j = validCPoints[i] + (int)params.minCDInterval;
         j < (int)(validCPoints[i] + params.maxCDInterval) && j < windowEnd;
         j++) {

      // APG signal validation - D point should correspond to APG secondary
      // positive peak
      bool isValidDAPG =
          (j >= 2) && (apgData[j] > 0.4) && // APG moderate positive value
          (apgData[j] > apgData[j - 1]) && (apgData[j] > apgData[j + 1]);

      if (filteredData[j] > maxD && filteredData[j] >= filteredData[j - 1] &&
          filteredData[j] >= filteredData[j + 1] && isValidDAPG) {
        maxD = filteredData[j];
        validDPoints[i] = j;
      }
    }

    // Feature point validation and waveform quality assessment
    float ab_amp =
        filteredData[validBPoints[i]] - filteredData[validAPoints[i]];
    float bc_amp =
        filteredData[validBPoints[i]] - filteredData[validCPoints[i]];
    float cd_amp =
        filteredData[validDPoints[i]] - filteredData[validCPoints[i]];

    float ab_interval = validBPoints[i] - validAPoints[i];
    float bc_interval = validCPoints[i] - validBPoints[i];
    float cd_interval = validDPoints[i] - validCPoints[i];

    // APG waveform quality validation
    bool validAPGShape =
        (apgData[validBPoints[i]] > 0.5) &&  // Main peak APG strength
        (apgData[validCPoints[i]] < -0.2) && // Secondary valley APG strength
        (apgData[validDPoints[i]] > 0.3);    // Secondary peak APG strength

    // Use adaptive parameters and APG quality validation
    if (ab_amp < params.minValidAmp || bc_amp < params.minValidAmp ||
        cd_amp < params.minValidAmp || ab_interval < params.minABInterval ||
        ab_interval > params.maxABInterval ||
        bc_interval < params.minBCInterval ||
        bc_interval > params.maxBCInterval ||
        cd_interval < params.minCDInterval ||
        cd_interval > params.maxCDInterval || !validAPGShape) {
      continue;
    }

    // Update historical data
    history.abIntervals[history.index] = ab_interval;
    history.bcIntervals[history.index] = bc_interval;
    history.cdIntervals[history.index] = cd_interval;
    history.amplitudes[history.index] = ab_amp;
    history.index = (history.index + 1) % HISTORY_SIZE;
  }

  // Update adaptive parameters
  if (validPeakCount > 0) {
    updateAdaptiveParams();
  }
}

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

void printFeaturePoints() {
  Serial.println("=== Feature Points Detection Results ===");
  Serial.print("Valid Peak Count: ");
  Serial.println(validPeakCount);

  for (int i = 0; i < validPeakCount; i++) {
    Serial.print("Peak #");
    Serial.println(i + 1);

    Serial.print("A Point: ");
    Serial.print(validAPoints[i]);
    Serial.print(" (APG: ");
    Serial.print(apgData[validAPoints[i]], 3);
    Serial.println(")");

    Serial.print("B Point: ");
    Serial.print(validBPoints[i]);
    Serial.print(" (APG: ");
    Serial.print(apgData[validBPoints[i]], 3);
    Serial.println(")");

    Serial.print("C Point: ");
    Serial.print(validCPoints[i]);
    Serial.print(" (APG: ");
    Serial.print(apgData[validCPoints[i]], 3);
    Serial.println(")");

    Serial.print("D Point: ");
    Serial.print(validDPoints[i]);
    Serial.print(" (APG: ");
    Serial.print(apgData[validDPoints[i]], 3);
    Serial.println(")");

    Serial.print("AB Interval: ");
    Serial.println(validBPoints[i] - validAPoints[i]);
    Serial.print("BC Interval: ");
    Serial.println(validCPoints[i] - validBPoints[i]);
    Serial.print("CD Interval: ");
    Serial.println(validDPoints[i] - validCPoints[i]);
    Serial.println("-------------------");
  }
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
  static unsigned long lastParamUpdateTime = 0;
  static unsigned long lastFeatureTime = 0;
  unsigned long currentTime = millis();

  // 10ms
  if (currentTime - lastSampleTime >= MIN_PROCESS_INTERVAL) {
    readAndPrintPPGData();
    applyMovingAverage();
    calculateAPG();
    lastSampleTime = currentTime;
  }

  // 100ms
  if (currentTime - lastDisplayTime >= MIN_DISPLAY_INTERVAL) {
    updateDisplayBuffer();
    displayPPGWaveform();
    lastDisplayTime = currentTime;
  }

  // 200ms
  if (currentTime - lastFeatureTime >= MIN_DETECT_INTERVAL) {
    detectFeatures();
#ifdef DEBUG
    printFeaturePoints();
#endif
    lastFeatureTime = currentTime;
  }

  // 500ms
  if (currentTime - lastParamUpdateTime >= MIN_UPDATE_INTERVAL) {
    updateAdaptiveParams();
    lastParamUpdateTime = currentTime;
  }
}