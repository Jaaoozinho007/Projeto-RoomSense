/*
  RoomSense - ESP32 como SERVIDOR HTTP na rede Wi-Fi local
  Sensores FC-51: ativo em LOW
  Dashboard: arquivos servidos pela LittleFS (pasta data/)

  API:
    GET  /api/status
    GET  /api/logs
    POST /api/logs/clear
    POST /api/led
    POST /api/mode
    POST /api/reset
    POST /api/entry
    POST /api/exit
*/

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ====== CONFIGURE SUA REDE WI-FI AQUI ======
const char* WIFI_SSID = "Ampernet-Benfica";
const char* WIFI_PASSWORD = "COLOQUE_SUA_SENHA_AQUI";

// ====== PINOS DO PROTOTIPO ======
const uint8_t SENSOR_ENTRADA = 27;
const uint8_t SENSOR_SAIDA = 26;
const uint8_t LED_VERMELHO = 18;
const uint8_t LED_VERDE = 19;

// OLED I2C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

bool oledReady = false;

WebServer server(80);
bool serverStarted = false;
bool littleFsReady = false;

const char* LOG_FILE = "/logs.txt";
const char* STATE_FILE = "/state.txt";

const int CAPACIDADE_MAXIMA = 10;

const unsigned long TEMPO_DEBOUNCE = 60;
const unsigned long TEMPO_SEQUENCIA = 2000;
const unsigned long INTERVALO_OLED = 250;
const unsigned long INTERVALO_RECONEXAO_WIFI = 10000;

int ocupacao = 0;
int totalEntradas = 0;
int totalSaidas = 0;

bool ledVermelhoLigado = false;
bool ledVerdeLigado = true;
bool modoManual = false;

String movimentacao = "parado";

unsigned long ultimaMovimentacao = 0;
unsigned long ultimaAtualizacaoOLED = 0;
unsigned long ultimaTentativaWiFi = 0;

// ====== SENSORES ======

struct SensorDebounce {
  uint8_t pin;
  bool rawActive;
  bool stableActive;
  unsigned long changedAt;
};

SensorDebounce sensorEntrada = {
  SENSOR_ENTRADA, false, false, 0
};

SensorDebounce sensorSaida = {
  SENSOR_SAIDA, false, false, 0
};

enum EstadoSequencia {
  AGUARDANDO,
  PRIMEIRO_ENTRADA,
  PRIMEIRO_SAIDA,
  AGUARDANDO_LIBERACAO
};

EstadoSequencia estadoSequencia = AGUARDANDO;

unsigned long inicioSequencia = 0;

// ====== LOGS ======

struct LogEntry {
  String time;
  String level;
  String message;
};

const uint8_t MAX_LOGS = 30;

LogEntry logs[MAX_LOGS];
uint8_t logCount = 0;

String uptimeText() {
  unsigned long seconds = millis() / 1000;

  char buffer[16];

  snprintf(
    buffer,
    sizeof(buffer),
    "%02lu:%02lu:%02lu",
    seconds / 3600,
    (seconds / 60) % 60,
    seconds % 60
  );

  return String(buffer);
}

String jsonEscape(const String& value) {
  String out;

  out.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];

    if (c == '"' || c == '\\') {
      out += '\\';
    }

    if (c == '\n') {
      out += ' ';
    } else if (c != '\r') {
      out += c;
    }
  }

  return out;
}

String cleanLogField(String value) {
  value.replace("|", "/");
  value.replace("\r", " ");
  value.replace("\n", " ");

  return value;
}

void persistLogs() {
  if (!littleFsReady) return;

  File file = LittleFS.open(LOG_FILE, "w");

  if (!file) {
    Serial.println("Falha ao gravar historico de LOG.");
    return;
  }

  for (uint8_t i = 0; i < logCount; i++) {
    file.print(cleanLogField(logs[i].time));
    file.print('|');

    file.print(cleanLogField(logs[i].level));
    file.print('|');

    file.println(cleanLogField(logs[i].message));
  }

  file.close();
}

void loadLogs() {
  if (!littleFsReady || !LittleFS.exists(LOG_FILE)) return;

  File file = LittleFS.open(LOG_FILE, "r");

  if (!file) return;

  logCount = 0;

  while (file.available()) {
    String line = file.readStringUntil('\n');

    line.trim();

    int first = line.indexOf('|');

    int second = (first >= 0)
      ? line.indexOf('|', first + 1)
      : -1;

    if (first < 0 || second < 0) continue;

    if (logCount >= MAX_LOGS) {
      for (uint8_t i = 1; i < MAX_LOGS; i++) {
        logs[i - 1] = logs[i];
      }

      logCount = MAX_LOGS - 1;
    }

    logs[logCount].time = line.substring(0, first);

    logs[logCount].level =
      line.substring(first + 1, second);

    logs[logCount].message =
      line.substring(second + 1);

    logCount++;
  }

  file.close();

  Serial.printf(
    "%u registros de LOG recuperados da memoria flash.\n",
    logCount
  );
}

void saveCounterState() {
  if (!littleFsReady) return;

  File file = LittleFS.open(STATE_FILE, "w");

  if (!file) {
    Serial.println("Falha ao salvar estado do contador.");
    return;
  }

  file.printf(
    "%d|%d|%d\n",
    ocupacao,
    totalEntradas,
    totalSaidas
  );

  file.close();
}

void loadCounterState() {
  if (!littleFsReady || !LittleFS.exists(STATE_FILE)) return;

  File file = LittleFS.open(STATE_FILE, "r");

  if (!file) return;

  String line = file.readStringUntil('\n');

  file.close();

  int first = line.indexOf('|');

  int second = (first >= 0)
    ? line.indexOf('|', first + 1)
    : -1;

  if (first < 0 || second < 0) return;

  int savedOccupancy =
    line.substring(0, first).toInt();

  int savedEntries =
    line.substring(first + 1, second).toInt();

  int savedExits =
    line.substring(second + 1).toInt();

  if (
    savedOccupancy < 0 ||
    savedOccupancy > CAPACIDADE_MAXIMA ||
    savedEntries < 0 ||
    savedExits < 0
  ) {
    Serial.println(
      "Estado salvo do contador invalido; iniciando em zero."
    );

    return;
  }

  ocupacao = savedOccupancy;
  totalEntradas = savedEntries;
  totalSaidas = savedExits;

  Serial.printf(
    "Contador recuperado: %d ocupantes, %d entradas, %d saidas.\n",
    ocupacao,
    totalEntradas,
    totalSaidas
  );
}

void addLog(const String& level, const String& message) {
  if (logCount >= MAX_LOGS) {
    for (uint8_t i = 1; i < MAX_LOGS; i++) {
      logs[i - 1] = logs[i];
    }

    logCount = MAX_LOGS - 1;
  }

  logs[logCount].time = uptimeText();
  logs[logCount].level = level;
  logs[logCount].message = message;

  logCount++;

  persistLogs();

  Serial.printf(
    "[%s] %s: %s\n",
    uptimeText().c_str(),
    level.c_str(),
    message.c_str()
  );
}

// ====== LEDS ======

void writeLeds() {
  digitalWrite(
    LED_VERMELHO,
    ledVermelhoLigado ? HIGH : LOW
  );

  digitalWrite(
    LED_VERDE,
    ledVerdeLigado ? HIGH : LOW
  );
}

void applyAutomaticLeds() {
  if (!modoManual) {
    ledVermelhoLigado =
      (ocupacao >= CAPACIDADE_MAXIMA);

    ledVerdeLigado =
      (ocupacao < CAPACIDADE_MAXIMA);

    writeLeds();
  }
}

// ====== LEITURA DOS SENSORES ======

void updateSensor(
  SensorDebounce &sensor,
  bool &risingDetection
) {
  bool readingActive =
    (digitalRead(sensor.pin) == LOW);

  if (readingActive != sensor.rawActive) {
    sensor.rawActive = readingActive;
    sensor.changedAt = millis();
  }

  if (
    (millis() - sensor.changedAt) >= TEMPO_DEBOUNCE &&
    sensor.stableActive != sensor.rawActive
  ) {
    sensor.stableActive = sensor.rawActive;

    if (sensor.stableActive) {
      risingDetection = true;
    }
  }
}

// ====== REGISTRO DE ENTRADAS E SAIDAS ======

void registerEntry(bool manual = false) {
  String origem = manual
    ? " pelo dashboard"
    : " pelo sensor fisico";

  if (ocupacao < CAPACIDADE_MAXIMA) {
    ocupacao++;
    totalEntradas++;

    saveCounterState();

    addLog(
      "success",
      "Entrada registrada" + origem +
      ". Ocupacao: " +
      String(ocupacao) + "/" +
      String(CAPACIDADE_MAXIMA) + "."
    );

  } else {
    addLog(
      "warning",
      "Entrada solicitada" + origem +
      ", mas a capacidade maxima ja foi atingida."
    );
  }

  movimentacao = "entrando";
  ultimaMovimentacao = millis();

  applyAutomaticLeds();
}

void registerExit(bool manual = false) {
  String origem = manual
    ? " pelo dashboard"
    : " pelo sensor fisico";

  if (ocupacao > 0) {
    ocupacao--;
    totalSaidas++;

    saveCounterState();

    addLog(
      "success",
      "Saida registrada" + origem +
      ". Ocupacao: " +
      String(ocupacao) + "/" +
      String(CAPACIDADE_MAXIMA) + "."
    );

  } else {
    addLog(
      "warning",
      "Saida solicitada" + origem +
      ", mas o ambiente ja esta vazio."
    );
  }

  movimentacao = "saindo";
  ultimaMovimentacao = millis();

  applyAutomaticLeds();
}

// ====== PROCESSAMENTO DOS SENSORES ======

void processSensors() {
  bool entradaEdge = false;
  bool saidaEdge = false;

  updateSensor(sensorEntrada, entradaEdge);
  updateSensor(sensorSaida, saidaEdge);

  if (estadoSequencia == AGUARDANDO) {

    if (entradaEdge && saidaEdge) return;

    if (entradaEdge) {
      estadoSequencia = PRIMEIRO_ENTRADA;
      inicioSequencia = millis();

      addLog(
        "info",
        "Sensor de entrada detectou movimento; aguardando segundo sensor."
      );

    } else if (saidaEdge) {
      estadoSequencia = PRIMEIRO_SAIDA;
      inicioSequencia = millis();

      addLog(
        "info",
        "Sensor de saida detectou movimento; aguardando segundo sensor."
      );
    }

  } else if (
    estadoSequencia == PRIMEIRO_ENTRADA &&
    saidaEdge
  ) {
    registerEntry();

    estadoSequencia = AGUARDANDO_LIBERACAO;

  } else if (
    estadoSequencia == PRIMEIRO_SAIDA &&
    entradaEdge
  ) {
    registerExit();

    estadoSequencia = AGUARDANDO_LIBERACAO;
  }

  if (
    (
      estadoSequencia == PRIMEIRO_ENTRADA ||
      estadoSequencia == PRIMEIRO_SAIDA
    ) &&
    millis() - inicioSequencia > TEMPO_SEQUENCIA
  ) {
    estadoSequencia = AGUARDANDO;

    addLog(
      "warning",
      "Sequencia de passagem expirou; movimento nao contabilizado."
    );
  }

  if (
    estadoSequencia == AGUARDANDO_LIBERACAO &&
    !sensorEntrada.stableActive &&
    !sensorSaida.stableActive
  ) {
    estadoSequencia = AGUARDANDO;
  }

  if (
    movimentacao != "parado" &&
    millis() - ultimaMovimentacao > 1800
  ) {
    movimentacao = "parado";
  }
}

// ====== OLED ======

void updateOLED() {
  if (
    !oledReady ||
    millis() - ultimaAtualizacaoOLED < INTERVALO_OLED
  ) {
    return;
  }

  ultimaAtualizacaoOLED = millis();

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("ROOMSENSE - AMBIENTE");

  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 17);

  display.print("Pessoas: ");
  display.println(ocupacao);

  display.setTextSize(1);
  display.setCursor(0, 42);

  display.print("Vagas: ");
  display.println(CAPACIDADE_MAXIMA - ocupacao);

  display.setCursor(0, 54);

  if (ocupacao >= CAPACIDADE_MAXIMA) {
    display.print("STATUS: LOTADO");

  } else if (ocupacao == 0) {
    display.print("STATUS: VAZIO");

  } else {
    display.print("STATUS: OCUPADO");
  }

  display.display();
}

// ====== JSON E API ======

String statusJson() {
  String json = "{";

  json += "\"occupancy\":" + String(ocupacao) + ",";
  json += "\"capacity\":" + String(CAPACIDADE_MAXIMA) + ",";
  json += "\"entries\":" + String(totalEntradas) + ",";
  json += "\"exits\":" + String(totalSaidas) + ",";

  json += "\"entryDetected\":";
  json += String(sensorEntrada.stableActive ? "true" : "false");
  json += ",";

  json += "\"exitDetected\":";
  json += String(sensorSaida.stableActive ? "true" : "false");
  json += ",";

  json += "\"ledRed\":";
  json += String(ledVermelhoLigado ? "true" : "false");
  json += ",";

  json += "\"ledGreen\":";
  json += String(ledVerdeLigado ? "true" : "false");
  json += ",";

  json += "\"mode\":\"";
  json += String(modoManual ? "manual" : "auto");
  json += "\",";

  json += "\"movement\":\"";
  json += movimentacao;
  json += "\"";

  json += "}";

  return json;
}

void sendJson(int code, const String& payload) {
  server.sendHeader("Cache-Control", "no-store");

  server.send(
    code,
    "application/json; charset=utf-8",
    payload
  );
}

void handleStatus() {
  sendJson(200, statusJson());
}

void handleLogs() {
  String json = "{\"logs\":[";

  for (int i = logCount - 1; i >= 0; i--) {
    if (i != logCount - 1) {
      json += ",";
    }

    json += "{\"time\":\"";
    json += jsonEscape(logs[i].time);

    json += "\",\"level\":\"";
    json += jsonEscape(logs[i].level);

    json += "\",\"message\":\"";
    json += jsonEscape(logs[i].message);

    json += "\"}";
  }

  json += "]}";

  sendJson(200, json);
}

// ====== COMANDOS DOS LEDS ======

void handleLedCommand() {
  if (!modoManual) {
    sendJson(
      409,
      "{\"error\":\"Ative o modo manual antes de controlar os LEDs.\"}"
    );

    return;
  }

  String color = server.arg("color");
  String value = server.arg("on");

  bool turnOn =
    (value == "1" || value == "true" || value == "on");

  if (color == "red") {
    ledVermelhoLigado = turnOn;

    addLog(
      "warning",
      String("Comando web: LED vermelho ") +
      (turnOn ? "ligado." : "desligado.")
    );

  } else if (color == "green") {
    ledVerdeLigado = turnOn;

    addLog(
      "success",
      String("Comando web: LED verde ") +
      (turnOn ? "ligado." : "desligado.")
    );

  } else {
    sendJson(
      400,
      "{\"error\":\"Cor invalida. Use red ou green.\"}"
    );

    return;
  }

  writeLeds();

  sendJson(200, statusJson());
}

void handleModeCommand() {
  String mode = server.arg("mode");

  if (mode == "manual") {
    modoManual = true;

    addLog(
      "info",
      "Modo manual ativado pelo dashboard."
    );

  } else if (mode == "auto") {
    modoManual = false;

    applyAutomaticLeds();

    addLog(
      "info",
      "Modo automatico ativado pelo dashboard."
    );

  } else {
    sendJson(
      400,
      "{\"error\":\"Modo invalido. Use auto ou manual.\"}"
    );

    return;
  }

  sendJson(200, statusJson());
}

// ====== LIMPAR LOGS ======

void handleClearLogs() {
  logCount = 0;

  if (
    littleFsReady &&
    LittleFS.exists(LOG_FILE)
  ) {
    LittleFS.remove(LOG_FILE);
  }

  sendJson(
    200,
    "{\"ok\":true,\"message\":\"Historico apagado.\"}"
  );
}

// ====== NOVOS COMANDOS MANUAIS ======

void handleManualEntry() {
  registerEntry(true);

  sendJson(200, statusJson());
}

void handleManualExit() {
  registerExit(true);

  sendJson(200, statusJson());
}

// ====== RESET DO CONTADOR ======

void handleReset() {
  ocupacao = 0;
  totalEntradas = 0;
  totalSaidas = 0;

  saveCounterState();

  estadoSequencia = AGUARDANDO;
  movimentacao = "parado";

  modoManual = false;

  applyAutomaticLeds();

  addLog(
    "warning",
    "Contador reiniciado pelo dashboard web."
  );

  sendJson(200, statusJson());
}

// ====== ARQUIVOS DO DASHBOARD ======

String contentTypeFor(const String& path) {
  if (path.endsWith(".html")) {
    return "text/html; charset=utf-8";
  }

  if (path.endsWith(".css")) {
    return "text/css; charset=utf-8";
  }

  if (path.endsWith(".js")) {
    return "application/javascript; charset=utf-8";
  }

  if (path.endsWith(".svg")) {
    return "image/svg+xml";
  }

  if (path.endsWith(".png")) {
    return "image/png";
  }

  if (path.endsWith(".ico")) {
    return "image/x-icon";
  }

  return "text/plain; charset=utf-8";
}

void handleFileRequest() {
  String path = server.uri();

  if (path == "/") {
    path = "/index.html";
  }

  if (path.indexOf("..") >= 0) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }

  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");

    server.sendHeader("Cache-Control", "no-cache");

    server.streamFile(file, contentTypeFor(path));

    file.close();

  } else {
    server.send(
      404,
      "text/plain; charset=utf-8",
      "Arquivo nao encontrado"
    );
  }
}

// ====== ROTAS HTTP ======

void configureRoutes() {
  server.on(
    "/api/status",
    HTTP_GET,
    handleStatus
  );

  server.on(
    "/api/logs",
    HTTP_GET,
    handleLogs
  );

  server.on(
    "/api/logs/clear",
    HTTP_POST,
    handleClearLogs
  );

  server.on(
    "/api/led",
    HTTP_POST,
    handleLedCommand
  );

  server.on(
    "/api/mode",
    HTTP_POST,
    handleModeCommand
  );

  server.on(
    "/api/reset",
    HTTP_POST,
    handleReset
  );

  server.on(
    "/api/entry",
    HTTP_POST,
    handleManualEntry
  );

  server.on(
    "/api/exit",
    HTTP_POST,
    handleManualExit
  );

  server.onNotFound(handleFileRequest);

  server.begin();

  serverStarted = true;

  addLog(
    "success",
    "Servidor HTTP iniciado na porta 80."
  );
}

// ====== WI-FI ======

void connectWiFi() {
  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Conectando ao Wi-Fi");

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 20000
  ) {
    delay(400);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi conectado.");

    Serial.print("IP do RoomSense: http://");

    Serial.println(WiFi.localIP());

    addLog(
      "success",
      "Conectado ao Wi-Fi. IP: " +
      WiFi.localIP().toString()
    );

  } else {
    Serial.println(
      "Wi-Fi ainda nao conectado. O firmware tentara novamente."
    );

    addLog(
      "warning",
      "Falha inicial no Wi-Fi; aguardando nova tentativa."
    );
  }
}

// ====== SETUP ======

void setup() {
  Serial.begin(115200);

  delay(300);

  pinMode(SENSOR_ENTRADA, INPUT);
  pinMode(SENSOR_SAIDA, INPUT);

  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);

  sensorEntrada.rawActive =
    sensorEntrada.stableActive =
    (digitalRead(SENSOR_ENTRADA) == LOW);

  sensorSaida.rawActive =
    sensorSaida.stableActive =
    (digitalRead(SENSOR_SAIDA) == LOW);

  sensorEntrada.changedAt = millis();
  sensorSaida.changedAt = millis();

  ledVermelhoLigado = false;
  ledVerdeLigado = true;

  writeLeds();

  Wire.begin(21, 22);

  oledReady = display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  );

  if (oledReady) {
    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);

    display.println("ROOMSENSE iniciando...");

    display.display();

  } else {
    Serial.println(
      "OLED nao encontrado; sistema continuara sem display."
    );
  }

  // false evita formatar automaticamente a flash caso a montagem falhe.
  if (!LittleFS.begin(false)) {
    Serial.println(
      "Falha ao montar LittleFS. Dados persistentes indisponiveis."
    );

  } else {
    littleFsReady = true;

    Serial.println("LittleFS montada.");

    loadLogs();
    loadCounterState();
  }

  applyAutomaticLeds();

  addLog(
    "info",
    "Firmware RoomSense iniciado."
  );

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    configureRoutes();
  }

  updateOLED();
}

// ====== LOOP PRINCIPAL ======

void loop() {
  if (WiFi.status() == WL_CONNECTED) {

    if (!serverStarted) {
      configureRoutes();
    }

    server.handleClient();

  } else if (
    millis() - ultimaTentativaWiFi >=
    INTERVALO_RECONEXAO_WIFI
  ) {
    ultimaTentativaWiFi = millis();

    WiFi.disconnect();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  processSensors();

  updateOLED();

  delay(2);
}
