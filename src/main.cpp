#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define USART_RX_BUF_SIZE 128
uint8_t USART_RX_BUF[USART_RX_BUF_SIZE];

void testdrawstyles(void) {
  display.clearDisplay();

  display.setTextSize(1);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner
  display.println(F("Hello, world!!!"));

  display.display();
  delay(2000);
}

void OLED_ShowNum(int x, int y, int num, int len, uint8_t size, uint8_t color) {
  display.setTextSize(size);
  display.setTextColor(color);
  display.setCursor(x, y);

  // 确保数字长度符合要求
  char str[10];
  sprintf(str, "%0*d", len, num); // 格式化数字，确保长度为len
  display.print(str);
}

void drawWaveform() {
  display.clearDisplay();

  // 绘制波形
  for (int i = 1; i < 65; i++) { // 从第二个字节开始
    int x = map(i - 1, 0, 63, 0, SCREEN_WIDTH - 1); // 映射x坐标

    // 将有符号数转换为无符号数
    int value = (int8_t)USART_RX_BUF[i];
    int y = map(value, -128, 127, SCREEN_HEIGHT - 1, 0); // 映射y坐标

    // 绘制点
    display.drawPixel(x, y, SSD1306_WHITE);
  }

  // 更新显示
  display.display();
}

void setup() {
  Serial.begin(38400); // 更改为常用的115200波特率
  Serial.write(0x8a);

  // 初始化OLED显示
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // 0x3C是OLED的I2C地址
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.display();
}

void loop() {
  static int dataCount = 0;

  if (Serial.available() > 0) {
    int incomingByte = Serial.read();

    if (dataCount == 0 && incomingByte != 0xFF) {
      // 如果第一个字节不是 0xFF，丢弃该字节
      return;
    }

    if (dataCount < 65) {
      USART_RX_BUF[dataCount++] = incomingByte;

      if (dataCount == 65) {
        // 接收到完整数据，处理数据
        drawWaveform();
        dataCount = 0;
        memset(USART_RX_BUF, 0, sizeof(USART_RX_BUF));
      }
    }
  }
}