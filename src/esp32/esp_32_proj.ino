
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==========================================
// CONFIGURACOES DO PROJETO
// ==========================================

// Sensores infravermelhos
#define SENSOR_ENTRADA 27
#define SENSOR_SAIDA   26

// LEDs
#define LED_VERMELHO 18
#define LED_VERDE    19

// OLED
#define LARGURA_TELA 128
#define ALTURA_TELA 64
#define OLED_RESET -1
#define OLED_ENDERECO 0x3C

// Sensores FC-51: LOW significa deteccao
#define DETECTADO LOW

// Capacidade maxima do ambiente
const int CAPACIDADE_MAXIMA = 10;

// Tempo entre a deteccao dos sensores
const unsigned long TEMPO_SEQUENCIA = 2000;

// Filtro contra oscilacoes dos sensores
const unsigned long TEMPO_DEBOUNCE = 60;

// Atualizacao do display
const unsigned long INTERVALO_DISPLAY = 250;


// ==========================================
// OBJETOS E VARIAVEIS
// ==========================================

Adafruit_SSD1306 display(
  LARGURA_TELA,
  ALTURA_TELA,
  &Wire,
  OLED_RESET
);

int ocupacao = 0;

enum EstadoContagem {
  AGUARDANDO,
  PRIMEIRO_ENTRADA,
  PRIMEIRO_SAIDA
};

EstadoContagem estado = AGUARDANDO;

unsigned long inicioSequencia = 0;
unsigned long ultimaAtualizacaoDisplay = 0;


// ==========================================
// CONTROLE DE DEBOUNCE
// ==========================================

struct Sensor {
  int pino;
  int leituraBruta;
  int leituraEstavel;
  unsigned long ultimaMudanca;
};

Sensor sensorEntrada;
Sensor sensorSaida;


// Inicializa o controle de um sensor
void inicializarSensor(Sensor &sensor, int pino) {

  sensor.pino = pino;

  pinMode(pino, INPUT);

  sensor.leituraBruta = digitalRead(pino);
  sensor.leituraEstavel = sensor.leituraBruta;

  sensor.ultimaMudanca = millis();
}


// Retorna true quando uma nova deteccao acontece
bool atualizarSensor(Sensor &sensor) {

  unsigned long agora = millis();

  int leitura = digitalRead(sensor.pino);

  if (leitura != sensor.leituraBruta) {

    sensor.leituraBruta = leitura;
    sensor.ultimaMudanca = agora;
  }

  if (
    agora - sensor.ultimaMudanca >= TEMPO_DEBOUNCE &&
    sensor.leituraEstavel != sensor.leituraBruta
  ) {

    sensor.leituraEstavel = sensor.leituraBruta;

    // Detecta somente a transicao para LOW
    if (sensor.leituraEstavel == DETECTADO) {
      return true;
    }
  }

  return false;
}


// ==========================================
// CONTROLE DA OCUPACAO
// ==========================================

void registrarEntrada() {

  if (ocupacao < CAPACIDADE_MAXIMA) {

    ocupacao++;

    Serial.print("Entrada registrada. Ocupacao: ");
    Serial.println(ocupacao);

  } else {

    Serial.println("Ambiente lotado! Entrada nao contabilizada.");
  }
}


void registrarSaida() {

  if (ocupacao > 0) {

    ocupacao--;

    Serial.print("Saida registrada. Ocupacao: ");
    Serial.println(ocupacao);

  } else {

    Serial.println("Ambiente vazio. Saida nao contabilizada.");
  }
}


// ==========================================
// IDENTIFICACAO DO SENTIDO
// ==========================================

void processarSequencia(
  bool entradaDetectada,
  bool saidaDetectada
) {

  unsigned long agora = millis();

  // Se a sequencia ultrapassar o tempo permitido,
  // ela sera descartada.
  if (
    estado != AGUARDANDO &&
    agora - inicioSequencia > TEMPO_SEQUENCIA
  ) {

    estado = AGUARDANDO;

    Serial.println("Sequencia expirada.");
  }


  // Dois sensores detectados no mesmo ciclo:
  // sentido ambiguo, portanto descartamos.
  if (entradaDetectada && saidaDetectada) {

    estado = AGUARDANDO;

    Serial.println("Deteccao simultanea. Sequencia descartada.");

    return;
  }


  // Maquina de estados
  switch (estado) {

    case AGUARDANDO:

      if (entradaDetectada) {

        estado = PRIMEIRO_ENTRADA;
        inicioSequencia = agora;

        Serial.println("Primeiro sensor: ENTRADA");

      } else if (saidaDetectada) {

        estado = PRIMEIRO_SAIDA;
        inicioSequencia = agora;

        Serial.println("Primeiro sensor: SAIDA");
      }

      break;


    case PRIMEIRO_ENTRADA:

      if (saidaDetectada) {

        registrarEntrada();

        estado = AGUARDANDO;
      }

      break;


    case PRIMEIRO_SAIDA:

      if (entradaDetectada) {

        registrarSaida();

        estado = AGUARDANDO;
      }

      break;
  }
}


// ==========================================
// CONTROLE DOS LEDS
// ==========================================

void atualizarLEDs() {

  if (ocupacao >= CAPACIDADE_MAXIMA) {

    // Ambiente lotado
    digitalWrite(LED_VERMELHO, HIGH);
    digitalWrite(LED_VERDE, LOW);

  } else {

    // Existem vagas disponiveis
    digitalWrite(LED_VERMELHO, LOW);
    digitalWrite(LED_VERDE, HIGH);
  }
}


// ==========================================
// ATUALIZACAO DO OLED
// ==========================================

void atualizarDisplay() {

  unsigned long agora = millis();

  if (
    agora - ultimaAtualizacaoDisplay <
    INTERVALO_DISPLAY
  ) {
    return;
  }

  ultimaAtualizacaoDisplay = agora;

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  // Titulo
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println("CONTADOR DE PESSOAS");

  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);


  // Ocupacao atual
  display.setTextSize(2);
  display.setCursor(0, 17);

  display.print(ocupacao);
  display.print("/");
  display.println(CAPACIDADE_MAXIMA);


  // Vagas disponiveis
  display.setTextSize(1);
  display.setCursor(0, 41);

  display.print("Vagas: ");
  display.println(CAPACIDADE_MAXIMA - ocupacao);


  // Situacao do ambiente
  display.setCursor(0, 53);

  if (ocupacao >= CAPACIDADE_MAXIMA) {

    display.println("Status: LOTADO");

  } else {

    display.println("Status: DISPONIVEL");
  }

  display.display();
}


// ==========================================
// INICIALIZACAO
// ==========================================

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" SISTEMA DE CONTROLE DE SALA");
  Serial.println("==============================");


  // Inicializa LEDs
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);

  digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(LED_VERDE, LOW);


  // Inicializa sensores
  inicializarSensor(sensorEntrada, SENSOR_ENTRADA);
  inicializarSensor(sensorSaida, SENSOR_SAIDA);


  // Inicializa comunicacao I2C
  Wire.begin(21, 22);


  // Inicializa OLED
  if (!display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ENDERECO
  )) {

    Serial.println("ERRO: OLED nao encontrado!");

    while (true) {
      delay(1000);
    }
  }


  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);

  display.println("Inicializando...");
  display.display();

  delay(1000);


  // Atualiza estado inicial
  atualizarLEDs();

  // Forca primeira atualizacao do display
  ultimaAtualizacaoDisplay =
    millis() - INTERVALO_DISPLAY;

  atualizarDisplay();


  Serial.println("Sistema iniciado com sucesso!");
  Serial.println("Aguardando deteccoes...");
}


// ==========================================
// LOOP PRINCIPAL
// ==========================================

void loop() {

  // Le os dois sensores
  bool entradaDetectada =
    atualizarSensor(sensorEntrada);

  bool saidaDetectada =
    atualizarSensor(sensorSaida);


  // Identifica entrada ou saida
  processarSequencia(
    entradaDetectada,
    saidaDetectada
  );


  // Atualiza os indicadores fisicos
  atualizarLEDs();

  // Atualiza informacoes do OLED
  atualizarDisplay();
}
