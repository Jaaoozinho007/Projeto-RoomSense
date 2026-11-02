
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// -------------------------------
// CONFIGURACAO
// -------------------------------

#define SENSOR_ENTRADA 27
#define SENSOR_SAIDA 26

#define LARGURA 128
#define ALTURA 64
#define OLED_RESET -1

#define CAPACIDADE_MAXIMA 10

// FC-51: normalmente LOW significa deteccao
#define DETECTADO LOW

Adafruit_SSD1306 display(
  LARGURA,
  ALTURA,
  &Wire,
  OLED_RESET
);

// -------------------------------
// VARIAVEIS
// -------------------------------

int pessoas = 0;

enum Estado {
  AGUARDANDO,
  PRIMEIRO_ENTRADA,
  PRIMEIRO_SAIDA
};

Estado estado = AGUARDANDO;

unsigned long tempoSequencia = 0;

const unsigned long TEMPO_MAXIMO = 1500;

// Controle de borda dos sensores
bool entradaAnterior = false;
bool saidaAnterior = false;

// -------------------------------
// DISPLAY
// -------------------------------

void atualizarDisplay() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(20, 0);
  display.println("SMART ROOM");

  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 19);
  display.print(pessoas);
  display.println(" PESSOAS");

  display.setTextSize(1);
  display.setCursor(0, 45);

  if (pessoas == 0) {
    display.println("SALA VAZIA");
  }
  else {
    display.println("SALA OCUPADA");
  }

  display.setCursor(0, 56);

  if (pessoas < CAPACIDADE_MAXIMA) {
    display.print("DISPONIVEL");
  }
  else {
    display.print("LOTADA");
  }

  display.display();
}

// -------------------------------
// CONTAGEM
// -------------------------------

void registrarEntrada() {

  pessoas++;

  if (pessoas > CAPACIDADE_MAXIMA) {
    pessoas = CAPACIDADE_MAXIMA;
  }

  Serial.println("ENTRADA DETECTADA!");

  atualizarDisplay();
}

void registrarSaida() {

  if (pessoas > 0) {
    pessoas--;
  }

  Serial.println("SAIDA DETECTADA!");

  atualizarDisplay();
}

// -------------------------------
// SETUP
// -------------------------------

void setup() {

  Serial.begin(115200);

  pinMode(SENSOR_ENTRADA, INPUT);
  pinMode(SENSOR_SAIDA, INPUT);

  Wire.begin(21, 22);

  if (!display.begin(
    SSD1306_SWITCHCAPVCC, 0x3C
  )) {

    Serial.println("Erro no OLED!");

    while (true);
  }

  Serial.println("SMART ROOM INICIADO!");

  atualizarDisplay();
}

// -------------------------------
// LOOP
// -------------------------------

void loop() {

  bool entradaAtiva =
    digitalRead(SENSOR_ENTRADA) == DETECTADO;

  bool saidaAtiva =
    digitalRead(SENSOR_SAIDA) == DETECTADO;

  // Detecta apenas o inicio de cada acionamento
  bool novoEventoEntrada =
    entradaAtiva && !entradaAnterior;

  bool novoEventoSaida =
    saidaAtiva && !saidaAnterior;

  entradaAnterior = entradaAtiva;
  saidaAnterior = saidaAtiva;

  // Primeira deteccao: entrada
  if (estado == AGUARDANDO) {

    if (novoEventoEntrada) {

      estado = PRIMEIRO_ENTRADA;
      tempoSequencia = millis();

    }
    else if (novoEventoSaida) {

      estado = PRIMEIRO_SAIDA;
      tempoSequencia = millis();

    }
  }

  // Sequencia de entrada: A -> B
  else if (estado == PRIMEIRO_ENTRADA) {

    if (novoEventoSaida) {

      registrarEntrada();
      estado = AGUARDANDO;

    }
  }

  // Sequencia de saida: B -> A
  else if (estado == PRIMEIRO_SAIDA) {

    if (novoEventoEntrada) {

      registrarSaida();
      estado = AGUARDANDO;

    }
  }

  // Expiracao da sequencia
  if (
    estado != AGUARDANDO &&
    millis() - tempoSequencia > TEMPO_MAXIMO
  ) {

    estado = AGUARDANDO;

    Serial.println("Sequencia expirada.");

  }

  delay(10);
}
