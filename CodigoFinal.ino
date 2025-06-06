#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Declaração de flags para agrupar booleanos (economia de RAM)
uint8_t flags = 0;
#define MODO_VERIFICACAO_SERIAL 0x01
#define SERVO_MOVING 0x02
#define SERVO_ABERTO_POR_ULTRASSOM 0x04
#define LEDS_ACESOS 0x08

SoftwareSerial fingerSerial(2, 3);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo portaServo;

const uint8_t posFechada = 90; // uint8_t para economizar memória
const uint8_t posAberta = 0;
const uint8_t MAX_NOME_LEN = 8;

const uint8_t pinLed1 = 8;  
const uint8_t pinLed2 = 7; 
const uint8_t pinLed3 = 6;
const uint8_t trigPin = 10;     
const uint8_t echoPin = 11;

unsigned long ultimoTempoMensagem = 0;
const unsigned long intervaloMensagem = 5000;
unsigned long servoStartTime = 0;
char ultimaMensagem[33] = ""; // Buffer para comparar mensagens do LCD

void atualizarLCD(const __FlashStringHelper* linha1, const char* linha2 = "");

void setup() {
  Serial.begin(9600);
  finger.begin(57600);
  lcd.init();
  lcd.backlight();

  pinMode(pinLed1, OUTPUT);
  pinMode(pinLed2, OUTPUT);
  pinMode(pinLed3, OUTPUT);
  digitalWrite(pinLed1, LOW);
  digitalWrite(pinLed2, LOW);
  digitalWrite(pinLed3, LOW);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  if (finger.verifyPassword()) {
    atualizarLCD(F("Sensor OK"));
  } else {
    atualizarLCD(F("Erro sensor!"));
    delay(5000); // Mantido delay, mas pode ser substituído por millis() se necessário
    atualizarLCD(F("Tente novamente"));
    return;
  }

  portaServo.attach(9);
  portaServo.write(posFechada);
  delay(1000); // Mantido delay para inicialização do servo
  portaServo.detach(); // Desanexar após inicialização

  atualizarLCD(F("Coloque o dedo"));
  printMenu();
}

void loop() {
  if (flags & SERVO_MOVING && millis() - servoStartTime >= 3000) {
    portaServo.attach(9); // Reanexar antes de mover
    portaServo.write(posFechada);
    delay(500); // Mantido delay para movimento suave
    portaServo.detach(); // Desanexar após uso
    flags &= ~SERVO_MOVING; // Desativar flag
    atualizarLCD(F("Coloque o dedo"));
    ultimoTempoMensagem = millis();
  }

  if (millis() - ultimoTempoMensagem > intervaloMensagem && !(flags & MODO_VERIFICACAO_SERIAL) && !(flags & SERVO_MOVING)) {
    atualizarLCD(F("Coloque o dedo"));
    ultimoTempoMensagem = millis();
  }

  if (Serial.available()) {
    char opcao = toupper(Serial.read());
    while (Serial.available()) Serial.read();

    if (opcao == 'C') {
      Serial.println(F("Digite um ID (1 a 127):"));
      char idStr[16];
      if (!lerEntradaSerial(idStr, sizeof(idStr), 10000)) {
        Serial.println(F("Timeout: Nenhum ID recebido."));
        printMenu();
        return;
      }
      uint8_t id = atoi(idStr);
      if (id < 1 || id > 127 || (atoi(idStr) == 0 && idStr[0] != '0')) {
        Serial.println(F("ID inválido."));
        printMenu();
        return;
      }

      Serial.print(F("Digite o nome (max "));
      Serial.print(MAX_NOME_LEN);
      Serial.println(F(" caracteres):"));
      char nome[MAX_NOME_LEN + 1];
      if (!lerEntradaSerial(nome, sizeof(nome), 10000)) {
        Serial.println(F("Timeout: Nenhum nome recebido."));
        printMenu();
        return;
      }

      uint8_t result = enrollFinger(id);
      if (result == FINGERPRINT_OK) {
        salvarNomeEEPROM(id, nome);
        Serial.println(F("Cadastro concluído!"));
      } else {
        Serial.print(F("Erro no cadastro: "));
        printFingerprintError(result);
      }
      printMenu();
    } else if (opcao == 'V') {
      flags |= MODO_VERIFICACAO_SERIAL;
      Serial.println(F("Coloque o dedo para verificar:"));
    } else if (opcao == 'O') {
      if (!(flags & SERVO_MOVING)) {
        atualizarLCD(F("Abertura manual"));
        Serial.println(F("Abertura manual forçada"));
        if (!(flags & LEDS_ACESOS)) {
          digitalWrite(pinLed1, HIGH);
          digitalWrite(pinLed2, HIGH);
          digitalWrite(pinLed3, HIGH);
          flags |= LEDS_ACESOS;
        }
        portaServo.attach(9);
        portaServo.write(posAberta);
        servoStartTime = millis();
        flags |= SERVO_MOVING;
      } else {
        Serial.println(F("Servo em movimento, tente novamente."));
      }
      printMenu();
    } else {
      Serial.println(F("Opção inválida."));
      printMenu();
    }
  }

  if (!(flags & SERVO_MOVING) && detectarPresencaUltrassom()) {
    abrirPortaPorUltrassom();
  }

  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz() != FINGERPRINT_OK) return;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) {
    if (!(flags & MODO_VERIFICACAO_SERIAL)) {
      atualizarLCD(F("Acesso negado"));
      Serial.println(F("ACESSO NEGADO"));
      delay(2000); // Mantido delay
      atualizarLCD(F("Coloque o dedo"));
    }
    flags &= ~MODO_VERIFICACAO_SERIAL;
    return;
  }

  uint8_t id = finger.fingerID;
  char nome[MAX_NOME_LEN + 1];
  lerNomeEEPROM(id, nome);

  if (flags & MODO_VERIFICACAO_SERIAL) {
    Serial.print(F("ID detectado: "));
    Serial.println(id);
    Serial.print(F("Nome: "));
    Serial.println(nome);
    flags &= ~MODO_VERIFICACAO_SERIAL;
    printMenu();
    return;
  }

  char linha2[17];
  snprintf(linha2, sizeof(linha2), "ID:%d %s", id, nome);
  atualizarLCD(F("Bem-vindo"), linha2);
  Serial.print(F("ACESSO PERMITIDO - ID:"));
  Serial.print(id);
  Serial.print(F(" - Nome:"));
  Serial.println(nome);

  if (!(flags & LEDS_ACESOS)) {
    digitalWrite(pinLed1, HIGH);
    digitalWrite(pinLed2, HIGH);
    digitalWrite(pinLed3, HIGH);
    flags |= LEDS_ACESOS;
  }

  portaServo.attach(9);
  portaServo.write(posAberta);
  servoStartTime = millis();
  flags |= SERVO_MOVING;
}

bool lerEntradaSerial(char* entrada, uint8_t tamanho, unsigned long timeout) {
  int i = 0;
  unsigned long startTime = millis();
  while (i < tamanho - 1 && millis() - startTime < timeout) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') break;
      entrada[i++] = c;
    }
  }
  entrada[i] = '\0';
  return i > 0;
}

void abrirPortaPorUltrassom() {
  if (flags & SERVO_ABERTO_POR_ULTRASSOM || flags & SERVO_MOVING) return;

  atualizarLCD(F("Abertura auto..."));
  Serial.println(F("Abertura automática por ultrassom"));

  digitalWrite(pinLed1, LOW);
  digitalWrite(pinLed2, LOW);
  digitalWrite(pinLed3, LOW);
  flags &= ~LEDS_ACESOS;

  portaServo.attach(9);
  portaServo.write(posAberta);
  servoStartTime = millis();
  flags |= SERVO_MOVING;
  flags |= SERVO_ABERTO_POR_ULTRASSOM;

  delay(1000); // Mantido delay
}

bool detectarPresencaUltrassom() {
  long duracao;
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duracao = pulseIn(echoPin, HIGH, 30000);
  if (duracao == 0) {
    Serial.println(F("Erro: Sensor ultrassônico não respondeu."));
    flags &= ~SERVO_ABERTO_POR_ULTRASSOM;
    return false;
  }
  long distancia = duracao * 0.034 / 2;
  if (distancia > 0 && distancia < 20) {
    return true;
  } else {
    flags &= ~SERVO_ABERTO_POR_ULTRASSOM;
    return false;
  }
}

void atualizarLCD(const __FlashStringHelper* linha1, const char* linha2 = "") {
  char novaLinha1[17], novaLinha2[17];
  strncpy_P(novaLinha1, (const char*)linha1, 16);
  novaLinha1[16] = '\0';
  strncpy(novaLinha2, linha2, 16);
  novaLinha2[16] = '\0';

  char novaMensagem[33];
  snprintf(novaMensagem, sizeof(novaMensagem), "%s%s", novaLinha1, novaLinha2);
  if (strcmp(novaMensagem, ultimaMensagem) != 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(novaLinha1);
    if (strlen(linha2) > 0) {
      lcd.setCursor(0, 1);
      lcd.print(novaLinha2);
    }
    strcpy(ultimaMensagem, novaMensagem);
  }
}

void printMenu() {
  Serial.println(F("Digite 'C' para cadastrar, 'V' para verificar ou 'O' para abrir manualmente:"));
}

uint8_t enrollFinger(uint8_t id) {
  int p = -1;
  Serial.println(F("Coloque o dedo..."));
  while (p != FINGERPRINT_OK) p = finger.getImage();
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return p;

  Serial.println(F("Remova o dedo."));
  delay(2000); // Mantido delay
  while (finger.getImage() != FINGERPRINT_NOFINGER);

  Serial.println(F("Coloque novamente..."));
  while ((p = finger.getImage()) != FINGERPRINT_OK);
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) return p;

  p = finger.createModel();
  if (p != FINGERPRINT_OK) return p;

  return finger.storeModel(id);
}

void salvarNomeEEPROM(uint8_t id, const char* nome) {
  int endereco = (id - 1) * MAX_NOME_LEN;
  if (endereco + MAX_NOME_LEN > EEPROM.length()) {
    Serial.println(F("Erro: Endereço excede tamanho da EEPROM!"));
    return;
  }
  for (int i = 0; i < MAX_NOME_LEN; i++) {
    char c = (i < strlen(nome)) ? nome[i] : '\0';
    EEPROM.update(endereco + i, c); // Usar update em vez de write
  }
}

void lerNomeEEPROM(uint8_t id, char* nome) {
  int endereco = (id - 1) * MAX_NOME_LEN;
  if (endereco + MAX_NOME_LEN > EEPROM.length()) {
    Serial.println(F("Erro: Endereço excede tamanho da EEPROM!"));
    strcpy(nome, "Erro EEPROM");
    return;
  }
  for (int i = 0; i < MAX_NOME_LEN; i++) {
    nome[i] = EEPROM.read(endereco + i);
    if (nome[i] < 32 || nome[i] > 126) nome[i] = ' ';
  }
  nome[MAX_NOME_LEN] = '\0';
}

void printFingerprintError(uint8_t p) {
  switch (p) {
    case FINGERPRINT_NOFINGER: Serial.println(F("Nenhum dedo detectado.")); break;
    case FINGERPRINT_IMAGEFAIL: Serial.println(F("Erro ao capturar imagem.")); break;
    case FINGERPRINT_IMAGEMESS: Serial.println(F("Imagem muito bagunçada.")); break;
    case FINGERPRINT_FEATUREFAIL: Serial.println(F("Falha na extração.")); break;
    case FINGERPRINT_ENROLLMISMATCH: Serial.println(F("As duas leituras não coincidem.")); break;
    case FINGERPRINT_BADLOCATION: Serial.println(F("Posição de ID inválida.")); break;
    case FINGERPRINT_FLASHERR: Serial.println(F("Erro na gravação.")); break;
    default: Serial.println(F("Erro desconhecido.")); break;
  }
}