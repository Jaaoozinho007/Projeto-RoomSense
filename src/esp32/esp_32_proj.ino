
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define LARGURA 128
#define ALTURA 64
#define OLED_RESET -1

Adafruit_SSD1306 display(
  LARGURA,
  ALTURA,
  &Wire,
  OLED_RESET
);

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);

  if (!display.begin(
    SSD1306_SWITCHCAPVCC, 0x3C
  )) {
    Serial.println("Erro ao iniciar OLED!");
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(15, 10);
  display.println("SMART ROOM");

  display.setTextSize(2);
  display.setCursor(10, 30);
  display.println("TESTE OK");

  display.display();

  Serial.println("OLED funcionando!");
}

void loop() {
}
