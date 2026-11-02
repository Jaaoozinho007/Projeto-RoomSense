
#define SENSOR_ENTRADA 27
#define SENSOR_SAIDA 26

void setup() {
  Serial.begin(115200);

  pinMode(SENSOR_ENTRADA, INPUT);
  pinMode(SENSOR_SAIDA, INPUT);

  Serial.println("=== TESTE DOS SENSORES IR ===");
}

void loop() {
  int entrada = digitalRead(SENSOR_ENTRADA);
  int saida = digitalRead(SENSOR_SAIDA);

  Serial.print("Entrada: ");
  Serial.print(entrada);

  Serial.print(" | Saida: ");
  Serial.println(saida);

  delay(250);
}
