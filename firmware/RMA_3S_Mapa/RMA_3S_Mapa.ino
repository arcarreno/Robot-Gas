/* =========================================================
   ROBOT AUTONOMO ESP32 - VERSION SIMPLE
   ---------------------------------------------------------
   Avanza con los motores. Si el frente mide menos de 50 cm:
   para, gira 8 pasos de 45 grados midiendo el frente, y sale
   por el rumbo mas despejado. Si hay gas, para y la app
   parpadea. Comandos por USB y por Supabase.

   Sin giroscopio, sin calibraciones, sin PID.
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
  #warning "Falta secrets.h: el robot arranca sin WiFi ni Supabase"
  struct RedWiFi { const char* ssid; const char* pass; };
  const RedWiFi REDES_WIFI[] = {{"", ""}};
  const int NUM_REDES_WIFI = 0;
  const char* SUPABASE_URL = "";
  const char* SUPABASE_CMD_URL = "";
  const char* SUPABASE_CMD_URL_BASE = "";
  const char* SUPABASE_KEY = "";
#endif

/* ===================== PINES ===================== */
const int EN_DER = 4,  IN1 = 12, IN2 = 14;   // motor derecho
const int EN_IZQ = 15, IN3 = 33, IN4 = 32;   // motor izquierdo
const int TRIG_F = 5,  ECHO_F = 18;
const int TRIG_L = 17, ECHO_L = 16;
const int TRIG_R = 25, ECHO_R = 34;
const int BTN_F  = 26;
const int MQ2_DO = 23, MQ2_AO = 36;
const int LED    = 2;
const int ENC_IZQ = 19, ENC_DER = 13;

const uint8_t MPU_ADDR = 0x68;
float gyroZ = 0.0f;
float gyroBias = 0.0f;
unsigned long tGyro = 0;

/* ===================== AJUSTES ===================== */
const int VEL_AVANCE = 190;
const int VEL_GIRO   = 170;
const float DIST_OBSTACULO = 50.0f;
const float DIST_LIBRE     = 300.0f;
// Velocidad estimada de avance para la odometria del mapa (cm/s a PWM 190).
const float CM_POR_SEG = 25.0f;

// Si al avanzar el robot gira en circulos en vez de ir recto, cambia UNO a -1.
const int SENTIDO_IZQ = 1;
const int SENTIDO_DER = 1;

const unsigned long T_TELEMETRIA = 2000;
const unsigned long T_COMANDOS   = 2000;
const unsigned long T_HTTP       = 3000;

/* ===================== ESTADO ===================== */
WiFiMulti wifi;
WiFiClientSecure tls;

bool moviendo = false;
float theta = 0.0f;
float dFrente = 0, dIzq = 0, dDer = 0;

bool gas = false, gasCorte = true, doUtil = false;
float ppm = 0;

volatile long encIzq = 0, encDer = 0;
long prevIzq = 0, prevDer = 0;
float x = 0.0f, y = 0.0f;

unsigned long tTelemetria = 0, tComandos = 0, tOdo = 0;
int resetReason = 0;

/* ===================== GIROSCOPIO ===================== */
void initMPU() {
  Wire.begin();
  Wire.setTimeOut(100);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00);      // despertar
  Wire.endTransmission(true);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B); Wire.write(0x08);      // +-500 dps
  Wire.endTransmission(true);
}

bool leerGyroZ() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x47);                        // GYRO_ZOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2) != 2) return false;
  int16_t raw = (Wire.read() << 8) | Wire.read();
  gyroZ = (raw / 65.5f) * DEG_TO_RAD;
  return true;
}

/* ===================== MOTORES ===================== */
void motores(int izq, int der) {
  izq *= SENTIDO_IZQ;
  der *= SENTIDO_DER;
  digitalWrite(IN1, der >= 0 ? HIGH : LOW);
  digitalWrite(IN2, der >= 0 ? LOW  : HIGH);
  ledcWrite(EN_DER, der >= 0 ? der : -der);

  digitalWrite(IN3, izq >= 0 ? HIGH : LOW);
  digitalWrite(IN4, izq >= 0 ? LOW  : HIGH);
  ledcWrite(EN_IZQ, izq >= 0 ? izq : -izq);
}

void parar() { motores(0, 0); }

/* ===================== ULTRASONIDO ===================== */
float medir(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(3);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long us = pulseIn(echo, HIGH, 18000UL);
  if (us == 0) return 0.0f;              // sin eco = via libre
  return us * 0.0343f / 2.0f;
}

float medirFrente() {
  float v[3]; int n = 0;
  for (int i = 0; i < 3; i++) {
    float d = medir(TRIG_F, ECHO_F);
    if (d > 2.0f) v[n++] = d;
    delay(6);
  }
  if (n == 0) return DIST_LIBRE;
  for (int i = 0; i < n - 1; i++)
    for (int j = i + 1; j < n; j++)
      if (v[j] < v[i]) { float t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[n / 2];
}

float medirLado(int trig, int echo) {
  float d = medir(trig, echo);
  return (d > 2.0f) ? d : 0.0f;
}

/* ===================== GAS ===================== */
void leerGas() {
  static unsigned long tMuestra = 0;
  static uint32_t histDO = 0;
  unsigned long ahora = millis();
  if (ahora - tMuestra < 50) return;
  tMuestra = ahora;

  int nivelDO = digitalRead(MQ2_DO);
  float v = analogRead(MQ2_AO) * (3.3f / 4095.0f);
  ppm = (v - 0.8f) * 800.0f;
  if (ppm < 0.0f) ppm = 0.0f;
  if (ppm > 9999.0f) ppm = 9999.0f;
  if (nivelDO == LOW) doUtil = true;

  histDO = (histDO << 1) | (nivelDO == HIGH ? 1UL : 0UL);
  uint8_t altos = 0;
  for (int i = 0; i < 24; i++) altos += (histDO >> i) & 1UL;

  if (doUtil && altos >= 20) gas = true;
  else if (altos <= 2)       gas = false;
}

/* ===================== MOVIMIENTO ===================== */
void atenderSerial();

/* Gira grados en el sitio. Se detiene cuando el MPU marca el angulo,
   no por tiempo fijo: ni se pasa ni se queda corto. */
void girar(int grados) {
  if (abs(grados) < 5) return;
  float objetivo = abs(grados) * DEG_TO_RAD;
  float acum = 0.0f;
  unsigned long t0 = millis(), tp = t0;
  unsigned long tope = abs(grados) * 22UL + 800UL;   // tope de seguridad

  if (grados > 0) motores(-VEL_GIRO, VEL_GIRO);
  else            motores( VEL_GIRO, -VEL_GIRO);

  while (millis() - t0 < tope) {
    if (leerGyroZ()) {
      unsigned long ahora = millis();
      acum += gyroZ * (ahora - tp) / 1000.0f;
      tp = ahora;
    }
    if (fabs(acum) >= objetivo) break;
    atenderSerial();
    leerGas();
    if (gas && gasCorte) break;
    if (!moviendo) break;
    delay(5);
  }
  parar();
  delay(40);
  tGyro = millis();
  theta += grados * DEG_TO_RAD;
  while (theta >  PI) theta -= 2 * PI;
  while (theta < -PI) theta += 2 * PI;
}

/* Gira hacia un lado SOLO hasta que el frente se despeja. No gira 90 fijos:
   si el hueco esta a 30 grados termina en 30, si no, sigue hasta 180.
   Es lo que hace el Maze-Solver de Moreshwar-R con 3 HC-SR04. */
void girarHastaDespejar(int sentido) {
  unsigned long t0 = millis(), tp = t0;
  float acum = 0.0f;

  if (sentido > 0) motores(-VEL_GIRO, VEL_GIRO);
  else            motores( VEL_GIRO, -VEL_GIRO);

  while (millis() - t0 < 3500UL) {
    if (leerGyroZ()) {
      unsigned long ahora = millis();
      acum += (gyroZ - gyroBias) * (ahora - tp) / 1000.0f;
      tp = ahora;
    }
    // No se concede terminar antes de 12 grados para no creer un eco
    // todavia pendiente del giro anterior.
    if (fabs(acum) > 12.0f * DEG_TO_RAD &&
        medirFrente() > DIST_OBSTACULO * 1.8f) break;
    atenderSerial();
    leerGas();
    if ((gas && gasCorte) || !moviendo) break;
    delay(8);
  }
  parar();
  delay(40);
  tGyro = millis();
  theta += acum;
  while (theta >  PI) theta -= 2 * PI;
  while (theta < -PI) theta += 2 * PI;
}

/* Evasion rapida: el frente esta tapado, se elige el LADO MAS DESPEJADO
   con los sensores izquierdo y derecho que ya estan montados. Solo si los
   dos lados estan tapados se hace el barrido completo de 8 pasos. */
void esquivar() {
  parar();

  // Un eco espurio no debe provocar un giro: se vuelve a medir antes.
  delay(60);
  if (medirFrente() > DIST_OBSTACULO) return;

  float li = (dIzq > 0.0f) ? dIzq : DIST_LIBRE;
  float de = (dDer > 0.0f) ? dDer : DIST_LIBRE;

  if (li > 70.0f || de > 70.0f) {
    if (li > de + 10.0f)      girarHastaDespejar(1);
    else if (de > li + 10.0f) girarHastaDespejar(-1);
    else if (li > 70.0f)      girarHastaDespejar(1);
    else                      girarHastaDespejar(-1);
    Serial.printf("Giro hasta despejar: izq %.0f der %.0f\n", li, de);
    return;
  }

  // Los dos lados tambien tapados: barrido corto de 8 pasos de 45.
  Serial.println("Barrido 360");
  float mejorDist = medirFrente();
  int mejorPaso = 0;
  for (int i = 1; i <= 8; i++) {
    girar(45);
    if (!moviendo || (gas && gasCorte)) return;
    float d = medirFrente();
    if (d > mejorDist) { mejorDist = d; mejorPaso = i; }
  }
  int giro = mejorPaso * 45;
  if (giro > 180) giro -= 360;
  if (mejorDist < 30.0f) girar(180);
  else if (giro != 0) girar(giro);
  Serial.printf("Evasion: mejor %.0f cm, paso %d\n", mejorDist, mejorPaso);
}

void actualizarLED() {
  unsigned long t = millis();
  if (gas)                  digitalWrite(LED, (t / 150) % 2);
  else if (moviendo)        digitalWrite(LED, HIGH);
  else                      digitalWrite(LED, (t / 800) % 2);
}

/* ===================== COMANDOS ===================== */
void iniciar() {
  parar();
  delay(100);
  theta = 0.0f;
  prevIzq = encIzq;
  prevDer = encDer;
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
    gasCorte = false; gas = false;
    Serial.println("ACK:GASOFF");
  } else if (cmd == "gason") {
    gasCorte = true;
    Serial.println("ACK:GASON");
  } else if (cmd == "mq2") {
    Serial.printf("MQ2: AO=%.2fV DO=%s doUtil=%d gas=%d\n",
                  analogRead(MQ2_AO) * 3.3f / 4095.0f,
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
    } else if (buf.length() < 32) buf += c;
    else buf = "";
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
  for (int i = 0; i < NUM_REDES_WIFI; i++)
    if (REDES_WIFI[i].ssid[0]) wifi.addAP(REDES_WIFI[i].ssid, REDES_WIFI[i].pass);

  Serial.print("WiFi");
  unsigned long t0 = millis();
  while (wifi.run() != WL_CONNECTED && millis() - t0 < 20000UL) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\nWiFi OK: %s | IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  else
    Serial.println("\nWiFi: sin conexion");
}

void enviarTelemetria() {
  if (WiFi.status() != WL_CONNECTED || SUPABASE_URL[0] == '\0') return;
  tls.setInsecure();
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(T_HTTP);
  if (!http.begin(tls, SUPABASE_URL)) { http.end(); tTelemetria = millis(); return; }
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
  if (!http.begin(tls, SUPABASE_CMD_URL)) { http.end(); tComandos = millis(); return; }
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);

  String cmd = "";
  long id = 0;
  if (http.GET() == 200) {
    String s = http.getString();
    int ic = s.indexOf("\"comando\":\"");
    int ii = s.indexOf("\"id\":");
    if (ic >= 0 && ii >= 0) {
      int ini = ic + 11, fin = s.indexOf('"', ini);
      if (fin > ini) cmd = s.substring(ini, fin);
      int p = ii + 5;
      while (p < (int)s.length() && isDigit(s[p])) { id = id * 10 + (s[p] - '0'); p++; }
    }
  }
  http.end();
  tComandos = millis();

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
  if (!http.begin(tls, base + "?procesado=eq.false")) { http.end(); return; }
  agregarCabeceras(http);
  http.sendRequest("PATCH", "{\"procesado\":true}");
  http.end();
}

void tareaNube() {
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long tRe = 0;
    if (millis() - tRe > 30000UL) { tRe = millis(); WiFi.reconnect(); }
    return;
  }

  // El PWM es hardware: NO hay que parar los motores para enviar HTTP.
  // Pararlos aqui era lo que hacia que avanzara 1 s y se detuviera 3 s.
  // Solo se espacia el HTTP para que no bloquee el loop cada 2 s.
  if (millis() - tTelemetria > 5000UL) enviarTelemetria();
  else if (millis() - tComandos > 5000UL) revisarComandos();
}

/* ===================== SETUP / LOOP ===================== */
void IRAM_ATTR isrEncIzq() { encIzq++; }
void IRAM_ATTR isrEncDer() { encDer++; }

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ROBOT SIMPLE ===");
  resetReason = (int)esp_reset_reason();
  Serial.printf("Reset: %d\n", resetReason);

  pinMode(EN_DER, OUTPUT); pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(EN_IZQ, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(TRIG_F, OUTPUT); pinMode(ECHO_F, INPUT);
  pinMode(TRIG_L, OUTPUT); pinMode(ECHO_L, INPUT);
  pinMode(TRIG_R, OUTPUT); pinMode(ECHO_R, INPUT);
  pinMode(BTN_F, INPUT_PULLUP);
  pinMode(ENC_IZQ, INPUT_PULLDOWN); pinMode(ENC_DER, INPUT_PULLDOWN);
  pinMode(MQ2_DO, INPUT_PULLUP); pinMode(MQ2_AO, INPUT);
  pinMode(LED, OUTPUT);

  ledcAttach(EN_DER, 5000, 8);
  ledcAttach(EN_IZQ, 5000, 8);
  parar();

  attachInterrupt(digitalPinToInterrupt(ENC_IZQ), isrEncIzq, RISING);
  attachInterrupt(digitalPinToInterrupt(ENC_DER), isrEncDer, RISING);

  initMPU();
  // Cero del giroscopio: 0.3 s quieto para quitar la deriva.
  float suma = 0.0f; int nM = 0;
  for (int i = 0; i < 40; i++) {
    if (leerGyroZ()) { suma += gyroZ; nM++; }
    delay(8);
  }
  gyroBias = (nM > 20) ? suma / nM : 0.0f;
  if (nM > 20) Serial.println("MPU OK");
  else Serial.println("MPU: no responde");
  tOdo = millis();
  tGyro = millis();

  conectarWiFi();
  descartarComandosViejos();

  dFrente = medirFrente();
  tTelemetria = 0;
  tComandos = 0;
  Serial.println("Listo");
}

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

    if (digitalRead(BTN_F) == LOW || (dFrente > 0.0f && dFrente < DIST_OBSTACULO)) {
      Serial.printf("Obstaculo a %.0f cm\n", dFrente);
      esquivar();
      tOdo = millis();
    } else {
      // Avance con correccion suave: si un lado esta mas cerca, girar un
      // poco hacia el otro lado para no pegarse a la pared.
      float li = (dIzq > 0.0f) ? dIzq : DIST_LIBRE;
      float de = (dDer > 0.0f) ? dDer : DIST_LIBRE;
      int corr = (int)((de - li) * 0.25f);
      if (corr > 30) corr = 30;
      if (corr < -30) corr = -30;
      motores(VEL_AVANCE + corr, VEL_AVANCE - corr);

      // Rumbo real del MPU: con la correccion el robot curvea, y theta
      // debe seguirlo para que el mapa no salga en linea recta.
      if (leerGyroZ()) {
        unsigned long ahora = millis();
        float dtg = (ahora - tGyro) / 1000.0f;
        tGyro = ahora;
        if (dtg > 0.0f && dtg < 0.3f) theta += (gyroZ - gyroBias) * dtg;
      }
      while (theta >  PI) theta -= 2 * PI;
      while (theta < -PI) theta += 2 * PI;

      // Odometria por tiempo. x/y estan en METROS (la telemetria los
      // multiplica por 100 para enviar cm).
      float dt = (millis() - tOdo) / 1000.0f;
      tOdo = millis();
      if (dt > 0.0f && dt < 0.3f) {
        float paso = (CM_POR_SEG / 100.0f) * dt;
        x += paso * cos(theta);
        y += paso * sin(theta);
      }
    }
  } else {
    parar();
    // Con el robot quieto se re-cera el gyro: evita que el rumbo del mapa
    // derive durante las esperas (tecnica del Maping-robot, MPU + filtro).
    static unsigned long tCero = 0;
    static float sumaCero = 0.0f;
    static int nCero = 0;
    if (millis() - tCero > 100UL) {
      tCero = millis();
      if (leerGyroZ()) { sumaCero += gyroZ; nCero++; }
      if (nCero >= 5) { gyroBias = sumaCero / nCero; sumaCero = 0.0f; nCero = 0; }
    }
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
