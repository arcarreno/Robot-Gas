/* =========================================================
   ROBOT AUTONOMO ESP32 - VERSION SIMPLE
   ---------------------------------------------------------
   Que hace:
     1. Avanza recto (corrige el rumbo con el giroscopio).
     2. Si el frente mide menos de 50 cm: se detiene, gira en el
        sitio 8 pasos de 45 grados midiendo el frente en cada uno,
        y sale por el rumbo mas despejado.
     3. Lee el MQ-2 (AO y DO) desde el primer segundo y lo manda a
        Supabase. Si hay gas, se detiene y la app parpadea.
     4. Recibe "ini" y "alto" por USB o por Supabase.

   Sin calibraciones de gas, sin PID de 3 terminos, sin modos.
   La unica calibracion es la de los motores, que se hace sola la
   primera vez que se pulsa "ini" (2 segundos, robot en el piso).
   ========================================================= */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "esp_system.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #warning "Falta secrets.h: el robot arrancara sin WiFi ni Supabase"
  struct RedWiFi { const char* ssid; const char* pass; };
  const RedWiFi REDES_WIFI[] = {{"", ""}};
  const int NUM_REDES_WIFI = 0;
  const char* SUPABASE_URL = "";
  const char* SUPABASE_CMD_URL = "";
  const char* SUPABASE_CMD_URL_BASE = "";
  const char* SUPABASE_KEY = "";
#endif

/* ===================== PINES ===================== */
// Motores (L298N)
const int EN_DER = 4,  IN1 = 12, IN2 = 14;   // motor derecho
const int EN_IZQ = 15, IN3 = 33, IN4 = 32;   // motor izquierdo

// Encoders (un canal: solo sirven para medir distancia)
const int ENC_IZQ = 19, ENC_DER = 13;

// Ultrasónicos
const int TRIG_F = 5,  ECHO_F = 18;
const int TRIG_L = 17, ECHO_L = 16;
const int TRIG_R = 25, ECHO_R = 34;

// Bumper frontal (solo emergencia)
const int BTN_F = 26;

// MQ-2
const int MQ2_DO = 23;   // salida digital (colector abierto: pull-up)
const int MQ2_AO = 36;   // salida analogica (ADC1; el ADC2 no va con WiFi)

// LED de estado
const int LED = 2;

// IMU
const uint8_t MPU_ADDR = 0x68;

/* ===================== AJUSTES ===================== */
const int VEL_AVANCE = 190;   // PWM de avance (0-255)
const int VEL_GIRO   = 150;   // PWM de pivote
const float DIST_OBSTACULO = 50.0f;   // cm: gatillo de la evasion
const float DIST_LIBRE     = 300.0f;  // cm: "sin eco" = via libre

const float AO_ALARMA = 2.40f;  // V del AO que se considera gas
const float AO_LIMPIO = 1.60f;  // V por debajo de los cuales se apaga

const unsigned long T_TELEMETRIA = 2000;
const unsigned long T_COMANDOS   = 2000;
const unsigned long T_HTTP       = 1200;

// Giro por tiempo si el giroscopio no responde (ms por grado)
const float MS_POR_GRADO = 13.0f;

/* ===================== ESTADO ===================== */
WiFiMulti wifi;
WiFiClientSecure tls;

bool moviendo = false;
float theta = 0.0f;          // rumbo [rad], 0 = el del arranque
float rumboRecto = 0.0f;     // rumbo que mantiene el avance recto
float gatillo = DIST_OBSTACULO;

float dFrente = 0, dIzq = 0, dDer = 0;

bool gas = false;
bool gasCorte = true;
bool doUtil = false;         // el DO sirve? (si esta saturado en LOW, no)
float ppm = 0;

int signoIzq = 1, signoDer = 1;   // polaridad real de cada motor
bool motoresCalibrados = false;
bool gyroOk = false;
int gyroSigno = 1;
float gyroBias = 0.0f;
float gyroZ = 0.0f;

volatile long encIzq = 0, encDer = 0;
long prevIzq = 0, prevDer = 0;
float x = 0.0f, y = 0.0f;
unsigned long tOdo = 0;

unsigned long tTelemetria = 0, tComandos = 0;

int resetReason = 0;

/* ===================== UTILIDADES ===================== */
void IRAM_ATTR isrEncIzq() { encIzq++; }
void IRAM_ATTR isrEncDer() { encDer++; }

float normalizar(float a) {
  while (a > PI)  a -= 2.0f * PI;
  while (a < -PI) a += 2.0f * PI;
  return a;
}

/* ===================== MOTORES ===================== */
void motoresRaw(int izq, int der) {
  // Derecho
  digitalWrite(IN1, der >= 0 ? HIGH : LOW);
  digitalWrite(IN2, der >= 0 ? LOW : HIGH);
  int vd = der >= 0 ? der : -der;
  ledcWrite(EN_DER, vd > 255 ? 255 : vd);

  // Izquierdo
  digitalWrite(IN3, izq >= 0 ? HIGH : LOW);
  digitalWrite(IN4, izq >= 0 ? LOW : HIGH);
  int vi = izq >= 0 ? izq : -izq;
  ledcWrite(EN_IZQ, vi > 255 ? 255 : vi);
}

// Positivo = adelante. Aplica la polaridad medida en la calibracion.
void motores(int izq, int der) {
  motoresRaw(izq * signoIzq, der * signoDer);
}

void parar() {
  motores(0, 0);
}

/* ===================== GIROSCOPIO ===================== */
bool leerGyroZ() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x47);                       // GYRO_ZOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2) != 2) return false;
  int16_t raw = (Wire.read() << 8) | Wire.read();
  gyroZ = (raw / 65.5f) * DEG_TO_RAD;     // escala +-500 dps
  return true;
}

void initMPU() {
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission(true);
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1B); Wire.write(0x08); Wire.endTransmission(true);
}

bool calibrarBias() {
  float suma = 0;
  int n = 0;
  for (int i = 0; i < 200; i++) {
    if (leerGyroZ()) { suma += gyroZ; n++; }
    delay(2);
  }
  if (n < 150) { gyroOk = false; return false; }
  gyroBias = suma / (float)n;
  gyroOk = true;
  return true;
}

/* Promedio del giroscopio durante una maniobra (radianes, con bias restado). */
float medirGiro(unsigned long ms) {
  float acum = 0;
  unsigned long t0 = millis(), tp = t0;
  while (millis() - t0 < ms) {
    if (leerGyroZ()) {
      unsigned long ahora = millis();
      acum += (gyroZ - gyroBias) * (ahora - tp) / 1000.0f;
      tp = ahora;
    }
    delay(4);
  }
  return acum;
}

void actualizarTheta() {
  unsigned long ahora = millis();
  float dt = (ahora - tOdo) / 1000.0f;
  if (dt <= 0.0f) return;
  tOdo = ahora;
  if (dt > 0.3f) return;
  if (!leerGyroZ()) return;
  theta = normalizar(theta + (gyroZ - gyroBias) * gyroSigno * dt);
}

/* ===================== CALIBRACION DE MOTORES =====================
   Un motor al reves hace que el robot gire en circulos en vez de
   avanzar. Aqui se mueve cada rueda por separado y se mira hacia
   donde rota el chasis: eso dice la polaridad real de cada motor.
   Se hace una sola vez, con el robot en el piso y espacio libre. */
float probarRaw(int izq, int der, unsigned long ms) {
  motoresRaw(izq, der);
  float d = medirGiro(ms);
  motoresRaw(0, 0);
  delay(180);
  return d;
}

void calibrarMotores() {
  if (motoresCalibrados) return;
  if (!gyroOk && !calibrarBias()) {
    Serial.println("CAL: sin giroscopio, no se pueden calibrar motores");
    motoresCalibrados = true;
    return;
  }

  Serial.println("CAL: probando motores (2 s, robot en el piso)...");
  float a = probarRaw( VEL_GIRO, 0, 260);        // izquierda "adelante"
  float b = probarRaw(0,  VEL_GIRO, 260);        // derecha "adelante"

  // Si las dos ruedas giran en el mismo sentido (mismo signo), una esta
  // invertida. Se prueban las dos correcciones y gana la que menos rota.
  if (fabs(a) > 0.05f && fabs(b) > 0.05f && a * b > 0.0f) {
    float c = probarRaw(-VEL_GIRO,  VEL_GIRO, 260);   // invertir izquierda
    float e = probarRaw( VEL_GIRO, -VEL_GIRO, 260);   // invertir derecha
    if (fabs(c) < fabs(e)) signoIzq = -1;
    else                   signoDer = -1;
    Serial.printf("CAL: motor invertido -> izq=%+d der=%+d\n", signoIzq, signoDer);
  }

  // Con los motores ya corregidos, un pivote a la izquierda debe aumentar
  // theta. Si el giroscopio esta montado al reves, se invierte su signo.
  float p = probarRaw(-VEL_GIRO * signoIzq, VEL_GIRO * signoDer, 300);
  if (fabs(p) < 0.08f) {
    gyroOk = false;
    Serial.println("CAL: el chasis no giro, se usaran giros por tiempo");
  } else {
    gyroSigno = (p > 0.0f) ? 1 : -1;
    Serial.printf("CAL: OK izq=%+d der=%+d gyro=%+d\n", signoIzq, signoDer, gyroSigno);
  }
  motoresCalibrados = true;
}

/* ===================== SENSORES ===================== */
float medir(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(3);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long us = pulseIn(echo, HIGH, 18000UL);  // 18 ms ~ 3 m
  if (us == 0) return 0.0f;              // sin eco
  return us * 0.0343f / 2.0f;
}

float medirFrente() {
  float v[3];
  int n = 0;
  for (int i = 0; i < 3; i++) {
    float d = medir(TRIG_F, ECHO_F);
    if (d > 2.0f) v[n++] = d;
    delay(6);
  }
  if (n == 0) return DIST_LIBRE;         // sin eco = via libre
  // mediana
  for (int i = 0; i < n - 1; i++)
    for (int j = i + 1; j < n; j++)
      if (v[j] < v[i]) { float t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[n / 2];
}

float medirLado(int trig, int echo) {
  float d = medir(trig, echo);
  return (d > 2.0f) ? d : 0.0f;
}

void leerGas() {
  int nivelDO = digitalRead(MQ2_DO);

  int adc = analogRead(MQ2_AO);
  float v = adc * (3.3f / 4095.0f);

  // Estimacion para la app (el MQ-2 no es lineal ni exacto).
  ppm = (v - 0.8f) * 800.0f;
  if (ppm < 0.0f) ppm = 0.0f;
  if (ppm > 9999.0f) ppm = 9999.0f;

  bool detecta = (v >= AO_ALARMA) || (doUtil && nivelDO == LOW);
  if (detecta) {
    gas = true;
  } else if (v <= AO_LIMPIO && (nivelDO == HIGH || !doUtil)) {
    gas = false;
  }
}

/* ===================== MOVIMIENTO ===================== */
void avanzar() {
  int corr = 0;
  if (gyroOk) {
    float err = normalizar(rumboRecto - theta);
    corr = (int)(err * 70.0f);
    if (corr > 35)  corr = 35;
    if (corr < -35) corr = -35;
  }
  int vel = VEL_AVANCE;
  if (dFrente > 0.0f && dFrente < 90.0f) {
    vel = (int)(90.0f + (dFrente / 90.0f) * (VEL_AVANCE - 90.0f));
  }
  motores(vel - corr, vel + corr);
}

/* Gira "grados" en el sitio. Positivo = izquierda. */
void girarGrados(float grados) {
  if (fabs(grados) < 1.0f) return;

  if (!gyroOk) {
    unsigned long ms = (unsigned long)(fabs(grados) * MS_POR_GRADO);
    if (grados > 0.0f) motores(-VEL_GIRO, VEL_GIRO);
    else               motores(VEL_GIRO, -VEL_GIRO);
    unsigned long t0 = millis();
    while (millis() - t0 < ms) { leerGas(); if (gas && gasCorte) break; delay(5); }
    parar();
    delay(120);
    tOdo = millis();                 // el giro ya se conto aqui: no re-integrar
    theta = normalizar(theta + grados * DEG_TO_RAD);
    return;
  }

  float objetivo = fabs(grados) * DEG_TO_RAD;
  float girado = 0.0f;
  unsigned long t0 = millis(), tp = t0;
  unsigned long tope = (unsigned long)(fabs(grados) * 30.0f) + 1500UL;

  while (millis() - t0 < tope) {
    if (leerGyroZ()) {
      unsigned long ahora = millis();
      girado += (gyroZ - gyroBias) * gyroSigno * (ahora - tp) / 1000.0f;
      tp = ahora;
    }
    float err = objetivo - girado;
    if (fabs(err) < 0.06f) break;                 // ~3.4 grados
    int v = (int)(fabs(err) * 110.0f);
    if (v < 80)  v = 80;
    if (v > VEL_GIRO) v = VEL_GIRO;
    if (err > 0.0f) motores(-v, v);
    else            motores(v, -v);

    leerGas();
    if (gas && gasCorte) break;
    delay(8);
  }
  parar();
  delay(130);
  tOdo = millis();                   // el giro ya se conto aqui: no re-integrar
  theta = normalizar(theta + girado);
}

/* Barrido de 360 en 8 pasos de 45 y salida por el mejor rumbo. */
void esquivar() {
  parar();
  delay(150);

  float mejorDist = medirFrente();
  float mejorAng = 0.0f;

  for (int i = 1; i <= 8; i++) {
    girarGrados(45.0f);
    if (gas && gasCorte) return;
    delay(100);
    float d = medirFrente();
    if (d > mejorDist) {
      mejorDist = d;
      mejorAng = i * 45.0f;
    }
  }

  // Estamos de vuelta en el rumbo inicial: ir al mejor.
  if (mejorAng > 180.0f) mejorAng -= 360.0f;
  if (mejorDist < 30.0f) {
    girarGrados(180.0f);              // todo cerrado: media vuelta
  } else if (fabs(mejorAng) >= 1.0f) {
    girarGrados(mejorAng);
  }

  rumboRecto = theta;
  // Si el mejor hueco era de 40 cm, no tiene sentido volver a barrer a los
  // 50: se baja el gatillo a la mitad de ese hueco (minimo 20 cm).
  gatillo = mejorDist * 0.5f;
  if (gatillo < 20.0f) gatillo = 20.0f;
  if (gatillo > DIST_OBSTACULO) gatillo = DIST_OBSTACULO;

  prevIzq = encIzq;
  prevDer = encDer;

  Serial.printf("Evasion: mejor %.0f cm, gatillo %.0f cm\n", mejorDist, gatillo);
}

/* ===================== LED ===================== */
void actualizarLED() {
  unsigned long t = millis();
  if (gas) {
    digitalWrite(LED, (t / 150) % 2);       // gas: parpadeo rapido
  } else if (moviendo) {
    digitalWrite(LED, HIGH);                // moviendo: fijo
  } else {
    digitalWrite(LED, (t / 800) % 2);       // reposo: latido lento
  }
}

/* ===================== COMANDOS ===================== */
void iniciar() {
  parar();
  delay(120);
  calibrarMotores();
  delay(200);
  calibrarBias();
  theta = 0.0f;
  rumboRecto = 0.0f;
  gatillo = DIST_OBSTACULO;
  prevIzq = encIzq;
  prevDer = encDer;
  tOdo = millis();
  moviendo = true;
  Serial.println("ACK:INI");
}

void ejecutarComando(String cmd) {
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "ini" || cmd == "start") {
    iniciar();
  } else if (cmd == "alto" || cmd == "stop") {
    moviendo = false;
    parar();
    Serial.println("ACK:ALTO");
  } else if (cmd == "ping") {
    Serial.println("ACK:PONG");
  } else if (cmd == "gasoff") {
    gasCorte = false;
    gas = false;
    Serial.println("ACK:GASOFF");
  } else if (cmd == "gason") {
    gasCorte = true;
    Serial.println("ACK:GASON");
  } else if (cmd == "cal") {
    motoresCalibrados = false;
    calibrarMotores();
    Serial.println("ACK:CAL");
  } else if (cmd == "mq2") {
    Serial.printf("MQ2: AO=%.2fV ppm=%.0f DO=%s doUtil=%d gas=%d\n",
                  analogRead(MQ2_AO) * 3.3f / 4095.0f, ppm,
                  digitalRead(MQ2_DO) == HIGH ? "HIGH" : "LOW",
                  (int)doUtil, (int)gas);
  } else {
    Serial.println("ERR:CMD " + cmd);
  }
}

void atenderSerial() {
  static String buf = "";
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (buf.length()) ejecutarComando(buf);
      buf = "";
    } else if (buf.length() < 32) {
      buf += c;
    } else {
      buf = "";
    }
  }
}

/* ===================== SUPABASE ===================== */
void agregarCabeceras(HTTPClient &http) {
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
}

void conectarWiFi() {
  for (int i = 0; i < NUM_REDES_WIFI; i++) {
    if (REDES_WIFI[i].ssid[0]) wifi.addAP(REDES_WIFI[i].ssid, REDES_WIFI[i].pass);
  }
  Serial.print("WiFi");
  unsigned long t0 = millis();
  while (wifi.run() != WL_CONNECTED && millis() - t0 < 20000UL) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK: %s | IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi: sin conexion (el robot funciona igual)");
  }
}

void enviarTelemetria() {
  if (WiFi.status() != WL_CONNECTED || SUPABASE_URL[0] == '\0') return;

  tls.setInsecure();
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(T_HTTP);
  if (!http.begin(tls, SUPABASE_URL)) { http.end(); return; }
  agregarCabeceras(http);

  String body = "{";
  body += "\"robot_id\":\"robot1\",";
  body += "\"x_cm\":" + String(x * 100.0f, 1) + ",";
  body += "\"y_cm\":" + String(y * 100.0f, 1) + ",";
  body += "\"theta_rad\":" + String(theta, 2) + ",";
  body += "\"front_cm\":" + String(dFrente, 1) + ",";
  body += "\"left_cm\":" + String(dIzq, 1) + ",";
  body += "\"right_cm\":" + String(dDer, 1) + ",";
  body += "\"gas_valor\":" + String(gas ? 1 : 0) + ",";
  body += "\"gas_alerta\":" + String(gas ? "true" : "false") + ",";
  body += "\"gas_ppm\":" + String(ppm, 0) + ",";
  body += "\"gas_do\":" + String(digitalRead(MQ2_DO) == LOW ? 0 : 1) + ",";
  body += "\"uptime_ms\":" + String(millis()) + ",";
  body += "\"reset_reason\":" + String(resetReason);
  body += "}";

  int code = http.POST(body);
  if (code != 201 && code != 200) Serial.printf("POST %d\n", code);
  http.end();
  tTelemetria = millis();
}

void revisarComandos() {
  if (WiFi.status() != WL_CONNECTED || SUPABASE_CMD_URL[0] == '\0') return;

  tls.setInsecure();
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(T_HTTP);
  if (!http.begin(tls, SUPABASE_CMD_URL)) { http.end(); return; }
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);

  String cmd = "";
  long id = 0;

  int code = http.GET();
  if (code == 200) {
    String s = http.getString();

    int ic = s.indexOf("\"comando\":\"");
    int ii = s.indexOf("\"id\":");
    if (ic >= 0 && ii >= 0) {
      int ini = ic + 11;
      int fin = s.indexOf('"', ini);
      if (fin > ini) cmd = s.substring(ini, fin);

      int p = ii + 5;
      while (p < (int)s.length() && isDigit(s[p])) { id = id * 10 + (s[p] - '0'); p++; }
    }
  }
  http.end();
  tComandos = millis();

  // Primero se cierra el GET y luego se ejecuta y se marca: asi el PATCH no
  // comparte el cliente TLS con una respuesta que sigue abierta.
  if (cmd.length()) ejecutarComando(cmd);

  if (id > 0) {
    String url = String(SUPABASE_CMD_URL_BASE) + id;
    HTTPClient patch;
    patch.setReuse(true);
    patch.setTimeout(T_HTTP);
    if (patch.begin(tls, url)) {
      agregarCabeceras(patch);
      patch.sendRequest("PATCH", "{\"procesado\":true}");
      patch.end();
    }
  }
}

void descartarComandosViejos() {
  if (WiFi.status() != WL_CONNECTED || SUPABASE_CMD_URL_BASE[0] == '\0') return;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(T_HTTP);
  String base = String(SUPABASE_CMD_URL_BASE);
  int q = base.indexOf('?');
  if (q > 0) base = base.substring(0, q);
  String url = base + "?procesado=eq.false";
  if (!http.begin(tls, url)) { http.end(); return; }
  agregarCabeceras(http);
  http.sendRequest("PATCH", "{\"procesado\":true}");
  http.end();
  Serial.println("Comandos viejos descartados");
}

void tareaNube() {
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long tRe = 0;
    if (millis() - tRe > 30000UL) { tRe = millis(); WiFi.reconnect(); }
    return;
  }

  bool despejado = !moviendo || dFrente > 100.0f;
  bool forzarTel = (millis() - tTelemetria > 8000UL);
  bool forzarCmd = (millis() - tComandos > 5000UL);

  // Cerca de una pared el robot no puede seguir avanzando a ciegas mientras
  // dura el POST. Se detiene un instante, publica y el loop reanuda.
  if (moviendo && (forzarTel || forzarCmd)) parar();

  if ((millis() - tTelemetria > T_TELEMETRIA && despejado) || forzarTel) {
    enviarTelemetria();
  } else if ((millis() - tComandos > T_COMANDOS && (!moviendo || dFrente > 60.0f)) || forzarCmd) {
    revisarComandos();
  }
}

/* ===================== SETUP ===================== */
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ROBOT SIMPLE ===");
  resetReason = (int)esp_reset_reason();
  Serial.printf("Reset: %d\n", resetReason);

  pinMode(EN_DER, OUTPUT); pinMode(EN_IZQ, OUTPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(TRIG_F, OUTPUT); pinMode(ECHO_F, INPUT);
  pinMode(TRIG_L, OUTPUT); pinMode(ECHO_L, INPUT);
  pinMode(TRIG_R, OUTPUT); pinMode(ECHO_R, INPUT);
  pinMode(BTN_F, INPUT_PULLUP);
  pinMode(ENC_IZQ, INPUT_PULLDOWN);
  pinMode(ENC_DER, INPUT_PULLDOWN);
  pinMode(MQ2_DO, INPUT_PULLUP);
  pinMode(MQ2_AO, INPUT);

  analogReadResolution(12);

  ledcAttach(EN_DER, 5000, 8);
  ledcAttach(EN_IZQ, 5000, 8);
  parar();

  attachInterrupt(digitalPinToInterrupt(ENC_IZQ), isrEncIzq, RISING);
  attachInterrupt(digitalPinToInterrupt(ENC_DER), isrEncDer, RISING);

  Wire.begin();
  Wire.setTimeOut(100);
  initMPU();
  delay(150);
  if (calibrarBias()) Serial.println("IMU OK");
  else Serial.println("IMU: no responde");

  // El DO del MQ-2 es de colector abierto. Si en aire limpio esta en LOW
  // (trimpot mal ajustado o pin flotante) no sirve como alarma: se ignora
  // y queda el AO. Asi el robot no se queda parado por un falso positivo.
  delay(100);
  doUtil = (digitalRead(MQ2_DO) == HIGH);
  Serial.printf("MQ2: DO inicial=%s -> %s\n",
                digitalRead(MQ2_DO) == HIGH ? "HIGH" : "LOW",
                doUtil ? "usable" : "ignorado (solo AO)");

  conectarWiFi();
  descartarComandosViejos();

  dFrente = medirFrente();
  tOdo = millis();
  tTelemetria = 0;
  tComandos = 0;
}

/* ===================== LOOP ===================== */
void loop() {
  atenderSerial();
  leerGas();
  actualizarLED();

  if (moviendo && gas && gasCorte) {
    moviendo = false;
    parar();
    Serial.println("GAS: robot detenido");
  }

  if (moviendo) {
    actualizarTheta();

    // Distancia recorrida (solo avance; en los pivotes no se cuenta)
    long ei = encIzq, ed = encDer;
    float d = ((ei - prevIzq) + (ed - prevDer)) * 0.5f * (2.0f * PI * 0.03f / 20.0f);
    prevIzq = ei;
    prevDer = ed;
    x += d * cos(theta);
    y += d * sin(theta);

    dFrente = medirFrente();

    static bool lado = false;
    if (lado) dIzq = medirLado(TRIG_L, ECHO_L);
    else      dDer = medirLado(TRIG_R, ECHO_R);
    lado = !lado;

    if (digitalRead(BTN_F) == LOW) {
      Serial.println("Bumper: esquivando");
      esquivar();
    } else if (dFrente > 0.0f && dFrente < gatillo) {
      Serial.printf("Obstaculo a %.0f cm\n", dFrente);
      esquivar();
    } else {
      avanzar();
    }
  } else {
    parar();
    static unsigned long tSensores = 0;
    if (millis() - tSensores > 500UL) {
      tSensores = millis();
      dFrente = medirFrente();
      dIzq = medirLado(TRIG_L, ECHO_L);
      dDer = medirLado(TRIG_R, ECHO_R);
    }
  }

  tareaNube();
  delay(15);
}
