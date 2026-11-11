#include <Wire.h>
#include "MPU6050.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <driver/ledc.h>
#include "BluetoothSerial.h"
// #include <ArduinoJson.h> // ELIMINADO para reducir flash (se usa String manual)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_wifi.h"   // esp_wifi_set_protocol: limitar a 11b/g ahorra RAM para el TLS
#include "esp_system.h" // esp_reset_reason: distinguir apagón de brownout

BluetoothSerial SerialBT;

/* =========================================================
   MODO DE OPERACIÓN
   USAR_WIFI 1 -> WiFi + Bluetooth (envío a Supabase)
   USAR_WIFI 0 -> SOLO Bluetooth (prueba de conexión estable)
   USAR_BLUETOOTH 1 -> inicia BT clásico (SPP)
   USAR_BLUETOOTH 0 -> modo WiFi puro: libera ~95 kB y permite
                       TLS + POST a Supabase a 5 Hz sin ventanas cloud
   ========================================================= */
#define USAR_WIFI 1
#define USAR_BLUETOOTH 0

// Prototipos
void M1_adelante(int x);
void M1_reversa(int x);
void M1_alto();
void M2_adelante(int y);
void M2_reversa(int y);
void M2_alto();
void enviarPosicion();
int velocidadProporcional(float distancia);
float leerSensorMediana(int triggerPin, int echoPin);
float medis(int triggerPin, int echoPin);
void enviarSupabase(bool forzar);
void descartarComandosPendientes();
void logDiag(const char* fmt, ...);
/* =========================
   === CONFIGURACIÓN HW ====
   ========================= */
const int LED_PIN = 2;

// Motores (puentes H)
const int enAM1 = 4; //PWM motor derecho
const int enBM2 = 15;//PWM motor izquierdo
const int IN1M1 = 12, IN2M1 = 14;//motor derecho
const int IN3M2 = 33, IN4M2 = 32;//motor izquierdo
//Encoders
const int TacL = 19, TacR = 13;
// Ultrasónicos (trig/echo)
const int TrigF = 5, EchoF = 18;
const int TrigR = 25, EchoR = 34;
const int TrigL = 17, EchoL = 16;
// PB colisiones
const int bL=35, bF=26, bR=27; 
/* =========================
   ======= VARIABLES =======
   ========================= */
// doc eliminado; se usa String manual para JSON
// Encoders y odometría
volatile long encoderLeft = 0;
volatile long encoderRight = 0;
volatile long choqueL=0, choqueF=0, choqueR=0; //variables conteo colisiones
bool bumperHit = false;                        // colisión pendiente de resolver
unsigned long tUltimoBumper = 0;
bool bFval = true, bLval = true, bRval = true; // pulsadores utilizables
unsigned int gasTransiciones = 0;              // cambios del DO del MQ-2 (diagnóstico)
bool mq2Confiable = false;                     // DO coherente en aire limpio
float x = 0.0f, y = 0.0f, theta = 0.0f;
int g_resetReason = 0;   // esp_reset_reason() de este arranque (9 = brownout)

// --- Navegación: rumbo recto con PID + evasión por pivote ---
enum EstadoEvasion : uint8_t { EV_AVANZAR, EV_ESCANEAR, EV_GIRAR, EV_RETROCEDER };
EstadoEvasion estadoEvasion = EV_AVANZAR;
float thetaObjetivo = 0.0f;      // rumbo que el PID intenta mantener [rad]
float integralGiro  = 0.0f;      // integral del error de rumbo [rad*s]
float anguloGiro    = 0.0f;      // rumbo destino del pivote [rad]
unsigned long tEvasion = 0;      // temporizador de la maniobra
unsigned long tFinEvasion = 0;   // instante en que termino la ultima evasion
bool g_pivoteInvertido = false;  // ruedas al reves respecto al signo de theta
bool g_signoPivoteConocido = false;
bool escaneoIniciado = false;    // el barrido 360 ya fijo su angulo de partida
/* Hueco reservado: el barrido 360 elige el rumbo con mas espacio. Si ese hueco
   queda entre DIST_CLEAR y DIST_ESCANEAR, el gatillo de "obstaculo" lo
   re-dispara al instante y el robot barre otra vez sin avanzar nunca. Con este
   valor el umbral baja a lo que realmente se midio, y solo vuelve a subir
   cuando el hueco se ha cerrado de verdad. */
float huecoReservado = 0.0f;

// Parámetros físicos
float r = 0.03f; // radio de rueda en metros
float L = 0.14f; // distancia entre ruedas en metros
int pulsosPorVuelta = 20;
float distancia_pulso = 2.0f * PI * r / pulsosPorVuelta;
// Variables booleanas
bool iniciarMovimiento = false;

// Estado de odometría por ciclo (se reinicia al arrancar)
bool estabaMoviendo = false;
long prevLeft = 0, prevRight = 0;
long g_deltaL = 0, g_deltaR = 0;

/* =========================
   ==== MQ-2 (D23 - DO) ====
   ========================= */
const int MQ2_PIN = 23;        // salida digital DO del MQ-2
// Polaridad en RUNTIME (ya no hay que recompilar para cambiarla):
// se autodetecta al terminar el calentamiento y se invierte con "gasinv".
int  mq2NivelActivo = LOW;     // nivel de DO que significa GAS
bool gasCorteActivo = true;    // "gasoff"/"gason" lo controlan por BT
bool mq2Calibrado = false;     // ya se midio R0 del AO en aire limpio
bool mq2PolaridadOk = false;   // ya se detecto la polaridad del DO
bool mq2ForzarPol = false;     // "gascal": forzar polaridad del DO
bool mq2ForzarR0 = false;      // "gascal": forzar R0 del AO
const int MQ2_AO_PIN = 36;     // salida analógica AO del MQ-2 -> GPIO36 (ADC1, solo entrada; ADC2 no funciona con WiFi)
// --- MQ-2 por AO (concentración ppm con histéresis y latch real) ---
const float MQ2_RL      = 5.0f;    // kΩ, resistencia de carga del módulo
float   mq2R0           = 10.0f;   // kΩ en aire limpio, se calibra al arrancar
const float MQ2_PPM_ALARMA = 300.0f;   // ppm a partir de los cuales alarma
const float MQ2_PPM_LIMPIO = 200.0f;   // ppm por debajo de los cuales se apaga (histéresis)
float   mq2UltimoPPM    = 0.0f;
bool    mq2AOConectado  = false;  // el AO lee algo? (si no, solo hay DO)
float   mq2DOFraction    = 0.0f;   // % de muestras del DO en LOW (para reevaluar)
bool    mq2LatchedAlarma = false;  // latch real: una vez alarma, solo se libera con gasoff/gascal
// El MQ-2 (calentador interno) tarda en estabilizarse: al arrancar da falsos
// positivos. Durante este tiempo se ignora la alarma. 3 min como mínimo.
// El DO (comparador) responde mucho antes que el AO (elemento quemado), asi que
// el DO se puede usar como alarma a los 20 s y el AO solo a los 3 min.
const unsigned long MQ2_CALENTAMIENTO_MS = 180000UL;
const unsigned long MQ2_CALENTAMIENTO_DO_MS = 20000UL;
const unsigned long MQ2_DEBOUNCE_MS = 1000UL;  // debe mantenerse para confirmar (1 s)
const unsigned long MQ2_INTERVALO_AO_MS = 250UL; // cadencia de la lectura analogica (4 Hz)
int mq2EstadoPrev = -1;        // ultimo nivel del DO, para contar transiciones
// Intervalo del aviso de gas sostenido. El corte NO se libera solo: la
// detección de gas es la única parada normal permitida.
const unsigned long MQ2_BLOQUEO_MAX_MS = 30000UL;
int gas_valor = 0;             // 0/1 en modo digital
bool gas_alerta = false;
unsigned long tGasDesde = 0;    // inicio de la alarma actual
bool btConectado = false;
unsigned long tHeartbeat = 0;   // latido por Serial (detecta reinicios)
unsigned long tAutoDiag = 0;    // diagnostico automatico al conectar por BT
bool modoCloud = false;         // true = BT liberado, subiendo telemetria a Supabase
unsigned long tCloudHasta = 0;  // fin de la ventana cloud
unsigned long tUltimoAutoCloud = 0; // para activar ventana cloud automática

// Emisor de diagnóstico: Serial siempre; por Bluetooth si hay cliente.
// Así el "Registro del enlace" de la app sirve de monitor aunque no se instale
// la APK con la consola de comandos.
void logDiag(const char* fmt, ...) {
  char buf[200];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
  if (SerialBT.hasClient()) {
    SerialBT.println(buf);
    SerialBT.flush();
    delay(15);   // deja respirar al stack BT para no perder líneas
  }
}

// Resultado de la última llamada a Supabase (0 = nunca se intentó)
int ultimoPostSupabase = 0;
int ultimoErrorTLS = 0;      // codigo de mbedTLS del ultimo fallo (-1 si fallo DNS/TCP)
unsigned long tUltimoFalloNube = 0;
// Tras un fallo se espera antes de reintentar: si el TLS está roto, no se
// deben encadenar timeouts y frenar el control del robot.
const unsigned long COOLDOWN_FALLO_MS = 15000UL;

/* =========================
   ==== WIFI / SUPABASE ====
   ========================= */
/* Los credenciales (WiFi y Supabase) viven en secrets.h, que NO se sube al
   repositorio. Copia secrets.example.h como secrets.h y rellena tus datos.
   Si compila sin secrets.h, usa los valores de ejemplo para que el sketch
   igual compile. */
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #warning "No existe secrets.h: se usan credenciales de ejemplo. Copia secrets.example.h como secrets.h."
  const char* WIFI_SSID = "TU_RED_WIFI";
  const char* WIFI_PASS = "TU_CONTRASENA_WIFI";
  const char* SUPABASE_BASE = "https://TU_PROYECTO.supabase.co";
  const char* SUPABASE_HOST = "TU_PROYECTO.supabase.co";
  const char* SUPABASE_URL = "https://TU_PROYECTO.supabase.co/rest/v1/lecturas_robot";
  const char* SUPABASE_CMD_URL = "https://TU_PROYECTO.supabase.co/rest/v1/comandos_robot?procesado=eq.false&robot_id=eq.robot1&order=id.asc&limit=1";
  const char* SUPABASE_CMD_URL_BASE = "https://TU_PROYECTO.supabase.co/rest/v1/comandos_robot?id=eq.";
  const char* SUPABASE_KEY = "sb_publishable_TU_CLAVE";
#endif

WiFiClientSecure supabaseClient;
unsigned long ultimoEnvioSupabase = 0;
unsigned long ultimoPollCmd = 0;
const unsigned long INTERVALO_SUPABASE = 2000; // 1 lectura cada 2 s (mapa fluido en la app)
const unsigned long INTERVALO_CMD = 2000;      // revisa comandos cada 2 s
const unsigned long TIMEOUT_NUBE_MS = 1800;     // nunca bloquear el control mas de 1.8 s
const unsigned long INTERVALO_SENSORES_REPOSO = 500; // sensores cuando el robot está detenido

// Ultrasónicos
float d_front = 0, d_left = 0, d_right = 0;
const float DIST_PANIC  = 14.0;     // cm: emergencia (bumper)
const float DIST_SLOW   = 60.0;     // cm: empieza a soltar velocidad
const float DIST_CLEAR  = 45.0;     // cm: frente despejado tras el escaneo
const float DIST_SIDE   = 25.0;     // cm: referencia de pasillo (informativa)
const float DIST_REVERSE = 10.0;    // cm: solo reversa si esta casi pegado
const float DIST_ESCANEAR = 50.0;   // cm: a esta distancia se detiene y escanea 360 grados
// Ventana de gracia tras terminar una evasion: sin ella, un frente de 46 cm
// rearma el barrido al instante (46 < DIST_ESCANEAR pero 46 >= DIST_CLEAR) y el
// robot se queda girando en el sitio sin avanzar nunca.
const unsigned long GRACIA_ESCANEO_MS = 1500UL;

// --- Escaneo 360° (8 x 45°) ---
const uint8_t PASOS_ESCANEO = 8;    // 8 x 45° = vuelta completa
const float ANG_PASO = 0.7854f;     // 45 grados
uint8_t escaneoPaso = 0;
float escaneoMejorDist = 0.0f;
float escaneoMejorAngulo = 0.0f;
float escaneoThetaInicio = 0.0f;
const int N = 3;                    // muestras del filtro de mediana (rápido)

// --- Pivote y PID de rumbo ---
const int   VEL_PIVOTE  = 170;      // PWM de cada rueda al girar en el lugar
const int   VEL_ESCANEO = 120;      // PWM del barrido 360: lento, el gyro no se pasa
const unsigned long TIMEOUT_GIRO_MS = 3500UL;  // tope por paso de giro
const float TOL_ANGULO  = 0.06f;    // ~3.4 grados: se considera logrado
const float KP_GIRO     = 110.0f;   // PWM por radián de error
const float KI_GIRO     = 55.0f;    // PWM por radián·segundo acumulado
const float KD_GIRO     = 20.0f;    // PWM por rad/s (amortiguación del gyro)

// Velocidades PWM
int vel_max = 240; 
int vel_min = 118;
int vel3 = 192;          
// Rango proporcional distancias
const float d_min = 15.0; 
const float d_max = 60.0; 

// IMU (MPU6050)
const uint8_t MPU_ADDR = 0x68;  // AD0 a GND
float gyroZ_offset = 0.0f;      // bias Z [rad/s]
float gyroZ_rads   = 0.0f;      // lectura actual Z [rad/s]

// Mezcla gyro/encoders para dTheta (0..1). El rumbo se integra SOLO con el
// giroscopio: los encoders son de un canal y no distinguen el sentido de giro.
float BLEND_YAW = 1.0f;
// Tiempos
unsigned long t_odo_ms = 0;
//interrupciones
void IRAM_ATTR handleEncoderL() { encoderLeft++; }
void IRAM_ATTR handleEncoderR() { encoderRight++; }

// IMU
void initMPU();
void calibrarGyroZ(int N);
void leerGyroZ();

/* =========================
   ==== MQ-2 / WIFI ========
   ========================= */
/* Lectura analogica del MQ-2. Devuelve el voltaje medio en V.
   Si el AO no esta conectado, el pin flotante lee ~0 V: eso se detecta y se
   avisa, para no reportar un ppm inventado (0 ppm = "aire limpio" cuando en
   realidad no hay sensor). */
float leerMQ2voltios() {
  long suma = 0;
  for (int i = 0; i < 10; i++) {
    suma += analogRead(MQ2_AO_PIN);
    delay(5);
  }
  return (suma / 10.0f) / 4095.0f * 3.3f;
}

/* Concentracion en ppm a partir del voltaje del AO.
   Rs = MQ2_RL * (3.3 - v) / v   (divisor de tension del modulo)
   ppm = 1000 * (Rs / R0) ^ -2.3  (curva LPG de la hoja de datos) */
float leerMQ2voltios();
float leerMQ2ppm() {
  float v = leerMQ2voltios();
  if (v <= 0.05f) return -1.0f;   // AO desconectado: no hay lectura valida
  if (v >= 3.25f) return 5000.0f; // AO saturado: hay mucho gas
  float rs = MQ2_RL * (3.3f - v) / v;
  return 1000.0f * pow(rs / mq2R0, -2.3f);
}

void leerMQ2() {
  /* Dos calentamientos distintos: el DO es un comparador y responde a los
     ~20 s, pero el AO (elemento quemado) necesita 3 min. Por eso el DO ya
     puede frenar al robot desde el minuto 1 y el AO entra despues. */
  bool calentandoAO = millis() < MQ2_CALENTAMIENTO_MS;
  bool calentandoDO = millis() < MQ2_CALENTAMIENTO_DO_MS;

  /* Leer el AO cuesta ~50 ms (10 muestras con delay). Hacerlo en cada vuelta del
     loop frenaba el control y la telemetria, asi que se limita a 4 Hz: sigue
     siendo de sobra para el debounce de 1 s. Antes de calibrar R0 el ppm no
     significa nada, se reporta -1 y no se gasta tiempo en medirlo.
     Si el AO no esta conectado, ppm queda en -1 y la alarma se apoya solo en
     el DO (no se inventa una concentracion). */
  static unsigned long tUltimaAO = 0;
  if (!mq2Calibrado || !mq2AOConectado) {
    mq2UltimoPPM = -1.0f;
  } else if (millis() - tUltimaAO >= MQ2_INTERVALO_AO_MS) {
    tUltimaAO = millis();
    mq2UltimoPPM = leerMQ2ppm();
  }
  float ppm = mq2UltimoPPM;

  /* AO: concentracion real. LATCH DE VERDAD. Antes el codigo decia que el corte
     "NO se libera solo" pero en cuanto el ppm bajaba de MQ2_PPM_LIMPIO el DO
     volvia a limpio, gas_alerta se apagaba y el robot reanudaba por su cuenta.
     Ahora, una vez enclavado, solo lo liberan "gasoff" (desactiva el corte) o
     "gascal" (recalibra R0 en aire limpio). Nunca se libera solo. */
  bool alarmaAO = (!calentandoAO && mq2Calibrado && mq2AOConectado)
                  ? (mq2LatchedAlarma || ppm >= MQ2_PPM_ALARMA)
                  : false;

  // DO como segunda alarma por hardware (el trimpot fija su propio umbral)
  int estado = digitalRead(MQ2_PIN);
  if (estado != mq2EstadoPrev) {
    mq2EstadoPrev = estado;
    if (gasTransiciones < 65535u) gasTransiciones++;
  }
  bool alarmaDO = (!calentandoDO && mq2Confiable) ? (estado == mq2NivelActivo) : false;

  bool detectado = alarmaAO || alarmaDO;

  // Confirmar solo si el estado se mantiene MQ2_DEBOUNCE_MS
  static unsigned long tPendiente = 0;
  static unsigned long tAvisoGas = 0;
  if (detectado == gas_alerta) {
    tPendiente = 0;
  } else if (tPendiente == 0) {
    tPendiente = millis();
  } else if (millis() - tPendiente >= MQ2_DEBOUNCE_MS) {
    gas_alerta = detectado;
    gas_valor = gas_alerta ? 1 : 0;
    tPendiente = 0;
    tAvisoGas = millis();
    if (gas_alerta) {
      tGasDesde = millis();
      /* Latch de verdad: una vez que alarma, no se apaga sola. Solo lo liberan
         "gasoff" (desactiva el corte) o "gascal" (recalibra en aire limpio). */
      mq2LatchedAlarma = true;
      logDiag("MQ-2: GAS DETECTADO (ppm=%.0f AO=%d DO=%d) -> ENCLAVADO, movimiento bloqueado",
              ppm, (int)alarmaAO, (int)alarmaDO);
    } else {
      logDiag("MQ-2: alarma liberada por el operador (ppm=%.0f)", ppm);
    }
  }

  /* Aviso periodico mientras dure el corte. MQ2_PPM_LIMPIO es la histeresis
     informativa: dice si el gas ya se disipo (el latch sigue activo) o si la
     concentracion continua alta, para saber si tiene sentido recalibrar. */
  if (gas_alerta && gasCorteActivo && millis() - tAvisoGas > MQ2_BLOQUEO_MAX_MS) {
    tAvisoGas = millis();
    logDiag("MQ2: gas enclavado %lu s | ppm=%.0f (%s) | 'gascal' recalibra, 'gasoff' libera",
            (millis() - tGasDesde) / 1000UL, ppm,
            (ppm >= 0.0f && ppm < MQ2_PPM_LIMPIO) ? "ya bajo del umbral limpio" : "sigue alto");
  }
}

/* Autodetección de polaridad del DO. Se hace PRONTO (20 s), no a los 3 min:
   el DO es un comparador y responde enseguida, así que desde el minuto 1 ya
   puede frenar al robot. El AO (elemento quemado) sí necesita 3 min y entra
   después con calibrarMQ2R0().
   - DO estable en HIGH en aire limpio -> el gas es LOW (caso normal).
   - DO fijo en LOW  -> trimpot al revés o gas constante: el DO se ignora como
     fuente de alarma (no se invierte a ciegas) y manda el AO.
   - DO que oscila   -> trimpot mal ajustado: el DO se ignora. */
void calibrarPolaridadMQ2() {
  if (mq2PolaridadOk) return;
  if (millis() < MQ2_CALENTAMIENTO_DO_MS && !mq2ForzarPol) return;
  if (iniciarMovimiento) {
    static unsigned long tAvisoPol = 0;
    if (millis() - tAvisoPol > 10000UL) {
      tAvisoPol = millis();
      logDiag("MQ2: polaridad EN ESPERA (robot en marcha). Detén con 'alto' o usa 'gascal'.");
    }
    return;
  }
  mq2ForzarPol = false;
  mq2PolaridadOk = true;

  const int MUESTRAS = 40;            // 40 x 50 ms = 2 s
  int bajos = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    if (digitalRead(MQ2_PIN) == LOW) bajos++;
    delay(50);
  }
  float frac = (float)bajos / (float)MUESTRAS;
  mq2DOFraction = frac;               // se guarda para reevaluar cuando llegue el AO

  if (frac <= 0.10f) {
    mq2NivelActivo = LOW;             // normal: aire limpio HIGH, gas LOW
    mq2Confiable = true;
    logDiag("MQ2: DO estable en HIGH (%.0f%% LOW) -> gas=LOW, DO activo", frac * 100.0f);
  } else if (frac >= 0.90f) {
    /* DO fijo en LOW. Con el pull-up puesto eso NO es un pin flotante: es que
       el comparador esta saturado. Causas probables:
         - el trimpot esta demasiado sensible (gira hasta que el LED DO se
           apague en aire limpio), o
         - el modulo NO esta alimentado (falta VCC): un LM393 sin VCC deja la
           salida a LOW indefinidamente, o
         - de verdad hay gas.
       Un DO saturado NO puede armar la alarma: si lo hiciera, el latch (que no
       se libera solo) dejaria al robot inservible sin motivo. Se descarta y se
       avisa. Si hay AO, la alarma real la da el ppm. */
    mq2NivelActivo = LOW;
    mq2Confiable = false;
    logDiag("MQ2: DO fijo en LOW (%.0f%%) -> comparador saturado, DO DESCARTADO.", frac * 100.0f);
    logDiag("    Revisa: (1) VCC del MQ-2 a 5V y GND comun, (2) gira el trimpot azul hasta que el LED DO se APAGUE en aire limpio, (3) conecta AO->GPIO36 si tu modulo tiene AO.");
    logDiag("    %s", mq2AOConectado
            ? "Alarma real: por AO (ppm)."
            : "SIN AO y con DO descartado: NO hay alarma de gas. Conecta el AO o ajusta el trimpot.");
  } else {
    mq2NivelActivo = LOW;
    mq2Confiable = false;             // oscila: no es confiable
    logDiag("MQ2: DO OSCILA (%.0f%% LOW) -> trimpot mal ajustado, DO DESCARTADO. Ajusta el trimpot hasta que el LED DO se apague.", frac * 100.0f);
  }
  mq2EstadoPrev = digitalRead(MQ2_PIN);
  gasTransiciones = 0;
}

/* Calibración de R0 (resistencia en aire limpio) del AO: una vez, tras el
   calentamiento largo de 3 min y con el robot detenido, en aire limpio. */
void calibrarMQ2R0() {
  if (mq2Calibrado) return;
  if (millis() < MQ2_CALENTAMIENTO_MS && !mq2ForzarR0) return;
  /* Hace falta el robot quieto. Si se mandó "ini" antes de que terminara el
     calentamiento, la calibración NO se pierde en silencio (antes dejaba el gas
     desactivado para siempre sin decir nada): se avisa cada 10 s y se reintenta
     en cuanto se detenga. */
  if (iniciarMovimiento) {
    static unsigned long tAvisoCalib = 0;
    if (millis() - tAvisoCalib > 10000UL) {
      tAvisoCalib = millis();
      logDiag("MQ2: calibración de R0 EN ESPERA (robot en marcha). Detén con 'alto' o usa 'gascal'.");
    }
    return;
  }
  mq2ForzarR0 = false;
  mq2Calibrado = true;

  /* Calibrar R0 en aire limpio: promedio de lecturas de AO.
     Durante los ~4 s de medición se sigue mandando telemetría y se atienden
     comandos, para que la app no se quede a oscuras (MS_SIN_DATOS = 15 s). */
  long sumaRs = 0;
  long sumaRaw = 0;
  int nValidas = 0;
  const int MUESTRAS = 60;
  for (int i = 0; i < MUESTRAS; i++) {
    long sumaAO = 0;
    for (int j = 0; j < 10; j++) { sumaAO += analogRead(MQ2_AO_PIN); delay(2); }
    float v = (sumaAO / 10.0f) / 4095.0f * 3.3f;
    sumaRaw += sumaAO / 10;
    if (v > 0.05f) {
      float rs = MQ2_RL * (3.3f - v) / v;
      sumaRs += (long)(rs * 1000.0f);
      nValidas++;
    }
    delay(50);
    if (i % 10 == 0) { enviarPosicion(); atenderComandos(); }
  }
  float vMedio = ((float)sumaRaw / MUESTRAS) / 4095.0f * 3.3f;
  if (nValidas > 0) {
    mq2R0 = (float)sumaRs / (float)nValidas / 1000.0f;
    mq2R0 = constrain(mq2R0, 1.0f, 100.0f);
    mq2AOConectado = true;
    logDiag("MQ2: R0 calibrado = %.1f kΩ (%d lecturas, AO=%.2f V). Alarma por AO activa desde %u ppm.",
            mq2R0, nValidas, vMedio, (unsigned)MQ2_PPM_ALARMA);
    /* Ahora que se sabe si hay AO, se reevalua el DO: si estaba saturado en LOW
       se descarta (manda el AO, que es la medida real). */
    if (mq2DOFraction >= 0.90f && mq2Confiable) {
      mq2Confiable = false;
      logDiag("MQ2: DO saturado en LOW -> descartado. Queda solo la alarma por AO (ppm).");
    }
  } else {
    /* El AO no lee nada: casi siempre es que el cable AO->GPIO36 no esta
       conectado (muchos modulos de 3 pines solo tienen DO). No se inventa un
       R0: se marca el AO como ausente y la alarma se apoya solo en el DO. */
    mq2AOConectado = false;
    logDiag("MQ2: AO SIN LECTURA (%.2f V). El cable AO->GPIO36 no esta conectado o el modulo es de 3 pines (solo DO). La alarma usara solo el DO.",
            vMedio);
  }
  gas_alerta = false;
  gas_valor = 0;
  tGasDesde = millis();
  mq2LatchedAlarma = false;
  mq2EstadoPrev = digitalRead(MQ2_PIN);
}
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // menos latencia y TLS más estable junto al BT
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi: conectando a \"%s\"\n", WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK | IP=%s | RSSI=%d dBm | canal=%d | heap=%u | max bloque=%u\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    // El 11n obliga al driver a reservar buffers de agregacion (RX reorder/BA).
    // Limitando a 11b/g se liberan varios kB que mbedTLS necesita para el TLS.
    esp_err_t pe = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
    delay(200);
    Serial.printf("WiFi 11b/g -> %s | heap=%u | max bloque=%u\n",
                  esp_err_to_name(pe), (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
  } else {
    int status = WiFi.status();
    Serial.printf("\nWiFi FALLO | status=%d\n", status);
    Serial.println("  status 1=NO_SSID 4=CONNECT_FAILED 6=DISCONNECTED");
  }
}

// Diagnóstico de red: separa DNS / TCP / HTTP / TLS para saber DÓNDE se rompe.
// Cada prueba dice OK o FALLO con el codigo exacto: no hay que adivinar.
void diagnosticoRed() {
  logDiag("Red: IP=%s | GW=%s | mascara=%s",
          WiFi.localIP().toString().c_str(),
          WiFi.gatewayIP().toString().c_str(),
          WiFi.subnetMask().toString().c_str());
  logDiag("AP: \"%s\" canal=%d RSSI=%d dBm BSSID=%s",
          WiFi.SSID().c_str(), WiFi.channel(), WiFi.RSSI(), WiFi.BSSIDstr().c_str());
  logDiag("Heap: libre=%u | max bloque=%u bytes",
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  IPAddress dnsDHCP = WiFi.dnsIP();
  logDiag("DNS entregado por DHCP: %s", dnsDHCP.toString().c_str());

  // 1) DNS con el servidor del router (el que dio el DHCP)
  IPAddress ip;
  unsigned long t0 = millis();
  bool dnsRouter = WiFi.hostByName(SUPABASE_HOST, ip);
  logDiag("DNS router (%s): %s (%lu ms)",
          dnsDHCP.toString().c_str(),
          dnsRouter ? ip.toString().c_str() : "FALLO", millis() - t0);

  // 2) DNS con 8.8.8.8 (descarta por completo al router)
  WiFi.setDNS(IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));
  delay(300);
  IPAddress ipPub;
  t0 = millis();
  bool dnsPub = WiFi.hostByName(SUPABASE_HOST, ipPub);
  logDiag("DNS 8.8.8.8: %s (%lu ms)",
          dnsPub ? ipPub.toString().c_str() : "FALLO", millis() - t0);

  if (!dnsRouter && !dnsPub) {
    logDiag("=> SIN DNS: el POST nunca podra salir (revisar DHCP/portal del AP)");
    WiFi.setDNS(dnsDHCP);
    return;
  }
  if (dnsPub) ip = ipPub;           // usar la que si resolvio
  else        WiFi.setDNS(dnsDHCP); // el router resolvio: dejar el DNS original
  logDiag("DNS elegido: %s", ip.toString().c_str());

  // 3) TCP puro al 443 (sin TLS): si esto va bien, el problema es el TLS
  WiFiClient tcp;
  bool tcpOk = tcp.connect(ip, 443, 3000);
  tcp.stop();
  logDiag("TCP 443: %s", tcpOk ? "OK" : "FALLO");

  // 4) HTTP plano a Google: comprueba salida real a internet (204 = limpio)
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(4000);
  http.setConnectTimeout(4000);
  if (http.begin("http://clients3.google.com/generate_204")) {
    int code = http.GET();
    if (code == 204) logDiag("HTTP plano google: 204 -> INTERNET OK");
    else logDiag("HTTP plano google: %d (%s)", code, http.errorToString(code).c_str());
    http.end();
  }

  // 5) HTTP plano a Supabase: 301 significa DNS+TCP+HTTP OK y solo falta TLS
  if (http.begin("http://vygwqsxyfdllirelndky.supabase.co/rest/v1/lecturas_robot")) {
    int code = http.GET();
    if (code == 301 || code == 308) logDiag("HTTP plano supabase: %d -> red OK, solo falta TLS", code);
    else logDiag("HTTP plano supabase: %d (%s)", code, http.errorToString(code).c_str());
    http.end();
  }

  // 6) TLS a mano con SNI, fuera de HTTPClient: aisla el handshake
  if (ESP.getFreeHeap() < 45000) {
    logDiag("TLS 443: OMITIDO (heap %u libre; con BT activo no caben los 2x16 kB de mbedTLS)",
            (unsigned)ESP.getFreeHeap());
    logDiag("  -> usa 'cloud 60': suelta el BT, sube a Supabase y lo devuelve");
    return;
  }
  NetworkClientSecure tls;
  tls.setInsecure();
  tls.setHandshakeTimeout(20);
  t0 = millis();
  int okTLS = tls.connect(SUPABASE_HOST, 443, 4000);
  char err[100] = {0};
  int errTLS = tls.lastError(err, sizeof(err));
  logDiag("TLS 443: %s (%lu ms) err=%d %s",
          okTLS == 1 ? "OK" : "FALLO", millis() - t0, errTLS, err);
  tls.stop();

  logDiag("Heap tras pruebas: %u bytes", (unsigned)ESP.getFreeHeap());
}

// Línea de estado compacta: se manda a Serial y a Bluetooth cada 20 s, así
// el "Registro del enlace" de la app muestra el estado real del robot.
void lineaEstado() {
  String s = "STATUS: up=" + String(millis() / 1000UL) + "s wifi=" +
             (WiFi.status() == WL_CONNECTED ? "OK" : "NO") +
             " ip=" + WiFi.localIP().toString() +
             " dns=" + WiFi.dnsIP().toString() +
             " heap=" + String(ESP.getFreeHeap() / 1024) + "k" +
             " post=" + String(ultimoPostSupabase) +
             " tls=" + String(ultimoErrorTLS) +
             " gas=" + String(gas_valor) +
             " DO=" + (digitalRead(MQ2_PIN) == HIGH ? "H" : "L") +
             " pol=" + (mq2NivelActivo == LOW ? "L" : "H") +
             " dist=" + String(d_front, 0) + "/" + String(d_left, 0) + "/" + String(d_right, 0) +
             " mov=" + String((int)iniciarMovimiento);
  Serial.println(s);
  if (SerialBT.hasClient()) {
    SerialBT.println(s);
    SerialBT.flush();
  }
}

void enviarSupabase(bool forzar) {
  if (WiFi.status() != WL_CONNECTED) {
    if (forzar) logDiag("Supabase: sin WiFi (status=%d)", (int)WiFi.status());
    return;
  }
  if (!forzar && millis() - ultimoEnvioSupabase < INTERVALO_SUPABASE) return;
  // Tras un fallo no se insiste: los timeouts encadenados frenaban el control
  if (!forzar && ultimoPostSupabase < 0 && millis() - tUltimoFalloNube < COOLDOWN_FALLO_MS) return;
  ultimoEnvioSupabase = millis();

  supabaseClient.setInsecure(); // pruebas; en produccion usa el CA de Supabase
  supabaseClient.setHandshakeTimeout(20);
  static const char *alpnHttp11[] = {"http/1.1", NULL};
  supabaseClient.setAlpnProtocols(alpnHttp11);
  HTTPClient http;
  http.setReuse(true);   // reutiliza la conexion TLS (clave para no cortar el BT)
  http.setTimeout(TIMEOUT_NUBE_MS);
  http.setConnectTimeout(TIMEOUT_NUBE_MS);  // el control del robot NUNCA se frena
  http.begin(supabaseClient, SUPABASE_URL);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String gasTxt = gas_alerta ? "true" : "false";
  String body = "{\"x_cm\":" + String(x * 100.0f, 1) + ",\"y_cm\":" + String(y * 100.0f, 1) + ",\"theta_rad\":" + String(theta, 2) + ",\"front_cm\":" + String(d_front, 1) + ",\"left_cm\":" + String(d_left, 1) + ",\"right_cm\":" + String(d_right, 1) + ",\"gas_valor\":" + String(gas_valor) + ",\"gas_alerta\":" + gasTxt + ",\"gas_ppm\":" + String(mq2UltimoPPM, 1) + ",\"gas_do\":" + String(digitalRead(MQ2_PIN)) + ",\"gas_pol\":" + String(!mq2Confiable ? 2 : (mq2NivelActivo == LOW ? 0 : 1)) + ",\"gas_n\":" + String(gasTransiciones) + ",\"uptime_ms\":" + String(millis()) + ",\"reset_reason\":" + String(g_resetReason) + ",\"robot_id\":\"robot1\"}";
  int code = http.POST(body);
  ultimoPostSupabase = code;
  if (code < 0) {
    tUltimoFalloNube = millis();
    char err[100] = {0};
    int errTLS = supabaseClient.lastError(err, sizeof(err));
    ultimoErrorTLS = errTLS;
    logDiag("Supabase POST: %d (%s) heap=%u max=%u | TLS err=%d %s",
            code, http.errorToString(code).c_str(),
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
            errTLS, err);
    supabaseClient.stop();   // limpia el socket TLS para el siguiente intento
  } else if (code != 201) {
    logDiag("Supabase POST: %d heap=%u", code, (unsigned)ESP.getFreeHeap());
  } else if (forzar) {
    logDiag("Supabase POST: 201 OK");
  }
  http.end();
}

void revisarComandos() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - ultimoPollCmd < (modoCloud ? 1000UL : INTERVALO_CMD)) return;
  // Mismo cooldown que el POST: si la nube esta caída, no se frena el robot
  if (ultimoPostSupabase < 0 && millis() - tUltimoFalloNube < COOLDOWN_FALLO_MS) return;
  ultimoPollCmd = millis();

  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(TIMEOUT_NUBE_MS);
  http.setConnectTimeout(TIMEOUT_NUBE_MS);
  http.begin(supabaseClient, SUPABASE_CMD_URL);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);

  int code = http.GET();
  if (code < 0) {
    Serial.printf("Supabase CMD GET: %d (%s)\n", code, http.errorToString(code).c_str());
    supabaseClient.stop();
  }
  if (code == 200) {
    String payload = http.getString();

    // Parseo manual del JSON (sin ArduinoJson para ahorrar flash)
    int idxCmd = payload.indexOf("\"comando\":\"");
    int idxId  = payload.indexOf("\"id\":");
    if (idxCmd >= 0 && idxId >= 0) {
      String comando = "";
      int start = idxCmd + 11;
      int end = payload.indexOf('"', start);
      if (end > start) comando = payload.substring(start, end);

      long id = 0;
      int idStart = idxId + 5;
      int p = idStart;
      while (p < payload.length() && isDigit(payload[p])) { id = id * 10 + (payload[p] - '0'); p++; }

      if (comando == "ini") iniciarMovimiento = true;
      if (comando == "alto") {
        iniciarMovimiento = false;
        M1_alto();
        M2_alto();
      }

      if (id > 0) {
        String url = String(SUPABASE_CMD_URL_BASE) + id;
        HTTPClient patch;
        patch.begin(supabaseClient, url);
        patch.addHeader("apikey", SUPABASE_KEY);
        patch.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
        patch.addHeader("Content-Type", "application/json");
        patch.addHeader("Prefer", "return=minimal");
        patch.sendRequest("PATCH", "{\"procesado\":true}");
        patch.end();
      }
    }
  }
  http.end();
}

/* =========================================================
   ==== MODO CLOUD (WiFi + Supabase, sin Bluetooth) ========
   El ESP32 no puede sostener a la vez BT clasico (95 kB) +
   WiFi (53 kB) + los dos buffers TLS de mbedTLS (33 kB):
   en total 181 kB de los 186 kB disponibles al arrancar.
   'cloud 60' suelta el Bluetooth, sube la telemetria a
   Supabase durante N segundos y despues lo devuelve.
   ========================================================= */

void salirModoCloud() {
  modoCloud = false;
  logDiag("Cloud: reactivando Bluetooth...");
  bool ok = SerialBT.begin("RobotBT", false, true);
  SerialBT.setPin("1234", 4);
  btConectado = false;
  logDiag("Cloud: BT %s | heap=%u", ok ? "OK" : "FALLO", (unsigned)ESP.getFreeHeap());
  if (!ok) {
    logDiag("Cloud: el BT no volvio, reiniciando la placa");
    delay(1000);
    ESP.restart();
  }
}

void entrarModoCloud(int segundos) {
  iniciarMovimiento = false;
  M1_alto();
  M2_alto();
  Serial.println("Cloud: liberando Bluetooth para el TLS...");
  if (SerialBT.hasClient()) {
    SerialBT.println("Cloud: cierro BT para subir a Supabase; pasa la app a modo WiFi");
    SerialBT.flush();
    delay(400);
  }
  SerialBT.end();          // devuelve ~95 kB al heap
  btConectado = false;
  delay(600);
  logDiag("Cloud: heap tras cerrar BT = %u | max=%u",
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  modoCloud = true;
  tCloudHasta = millis() + (unsigned long)segundos * 1000UL;
#if USAR_WIFI
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  enviarSupabase(true);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.printf("Chip: %s | heap total=%u | libre=%u | PSRAM=%u\n",
                ESP.getChipModel(), (unsigned)ESP.getHeapSize(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getPsramSize());
  Serial.println("=== ROBOT INICIADO ===");
  Serial.printf("Uptime tras boot: %lu ms\n", millis());
  // Motivo del arranque: si es BROWNOUT, la batería/regulador no aguanta el
  // pico de corriente (WiFi o motores) y el ESP32 se está reiniciando solo.
  const char *motivoReset = "DESCONOCIDO";
  g_resetReason = (int)esp_reset_reason();
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  motivoReset = "POWERON (encendido normal)"; break;
    case ESP_RST_BROWNOUT: motivoReset = "BROWNOUT (tension baja)"; break;
    case ESP_RST_SW:       motivoReset = "SOFTWARE"; break;
    case ESP_RST_PANIC:    motivoReset = "PANIC (excepcion)"; break;
    case ESP_RST_INT_WDT:  motivoReset = "WATCHDOG interno"; break;
    case ESP_RST_TASK_WDT: motivoReset = "WATCHDOG de tarea"; break;
    case ESP_RST_WDT:      motivoReset = "WATCHDOG"; break;
    case ESP_RST_DEEPSLEEP:motivoReset = "DEEPSLEEP"; break;
    case ESP_RST_EXT:      motivoReset = "RESET externo"; break;
    default:               motivoReset = "OTRO"; break;
  }
  Serial.printf("Reset: %s\n", motivoReset);

  // Bluetooth: SSP "Just Works" antes de begin() y PIN como respaldo despues.
  // disableBLE=true -> el controlador arranca SOLO en modo clasico (SPP).
  // Eso libera toda la RAM del BLE, que es justo lo que le faltaba al TLS:
  // mbedTLS necesita ~40 kB contiguos para el handshake HTTPS.
#if USAR_BLUETOOTH
  SerialBT.enableSSP(false, false);
  Serial.printf("Heap antes de BT: %u bytes\n", (unsigned)ESP.getFreeHeap());
  bool okBT = SerialBT.begin("RobotBT", false, true);
  bool okPin = SerialBT.setPin("1234", 4);
  Serial.printf("BT begin=%s | setPin=%s\n", okBT ? "OK" : "FALLO", okPin ? "OK" : "FALLO");
  Serial.printf("Heap despues de BT (solo clasico): %u bytes | max bloque=%u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
#else
  Serial.printf("Bluetooth DESACTIVADO (modo WiFi puro) | heap=%u\n",
                (unsigned)ESP.getFreeHeap());
#endif
  Serial.println("MQ-2 en GPIO23 (DO) y GPIO36 (AO)");

  pinMode(enAM1, OUTPUT);
  pinMode(enBM2, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TrigF, OUTPUT); pinMode(EchoF, INPUT);
  pinMode(TrigR, OUTPUT); pinMode(EchoR, INPUT);
  pinMode(TrigL, OUTPUT); pinMode(EchoL, INPUT);
  pinMode(IN1M1, OUTPUT); pinMode(IN2M1, OUTPUT);
  pinMode(IN3M2, OUTPUT); pinMode(IN4M2, OUTPUT);
  pinMode(TacL, INPUT_PULLDOWN);
  pinMode(TacR, INPUT_PULLDOWN);
  pinMode(bL,INPUT);        //PB izquierdo: GPIO35 solo entrada, sin pull-up interno
  pinMode(bF,INPUT_PULLUP); //PB frontal
  pinMode(bR,INPUT_PULLUP); //PB derecho
  /* El DO del MQ-2 es de COLECTOR ABIERTO: el comparador (LM393) solo puede
     BAJAR el pin a LOW. Para que en aire limpio se lea HIGH hace falta un
     pull-up. Por eso el pull-up interno del ESP32 NO se quita: sin el, el pin
     queda flotante y lee LOW siempre, que parece "gas permanente" pero es un
     falso. El pull-up interno (45 k) va en paralelo con el del modulo (10 k),
     asi que no sesga la lectura: solo impide que el pin flote.
     Con el pull-up puesto, un LOW si es real: el comparador esta cerrando. */
  pinMode(MQ2_PIN, INPUT_PULLUP);
  pinMode(MQ2_AO_PIN, INPUT); // AO del MQ-2 (lectura de concentración)

  // Un pulsador cerrado al arrancar está flotando o atascado: se ignora.
  // (GPIO35 no tiene pull-up interno, así que puede leerse LOW sin que nadie lo toque)
  delay(5);
  bFval = (digitalRead(bF) == HIGH);
  bLval = (digitalRead(bL) == HIGH);
  bRval = (digitalRead(bR) == HIGH);
  if (!bFval || !bLval || !bRval) {
    Serial.printf("AVISO: bumper ignorado (flotando/atascado) F=%d L=%d R=%d\n",
                  (int)bFval, (int)bLval, (int)bRval);
  }

  ledcAttach(enAM1, 5000, 8);
  ledcAttach(enBM2, 5000, 8);
// IMU
  Wire.begin();
  Wire.setTimeOut(100);   // si el MPU6050 no responde, fallar rapido (no 38 s)
  initMPU();
  delay(200);
  calibrarGyroZ(2000);  // robot QUIETO ~2 s
//Activación de interrupciones
  attachInterrupt(digitalPinToInterrupt(TacL), handleEncoderL, RISING); 
  attachInterrupt(digitalPinToInterrupt(TacR), handleEncoderR, RISING);  
  Serial.printf("Heap tras IMU: %u | max=%u | min=%u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                (unsigned)ESP.getMinFreeHeap());

  btConectado = false;
  Serial.println();
  Serial.println("=== ROBOT INICIADO ===");

  Serial.printf("MQ2: DO inicial=%s | calentando %lu s (alarma ignorada)\n",
                digitalRead(MQ2_PIN) == HIGH ? "HIGH" : "LOW",
                MQ2_CALENTAMIENTO_MS / 1000UL);

#if USAR_WIFI
  conectarWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    diagnosticoRed();
    // Con el Bluetooth activo no hay heap para los buffers TLS de mbedTLS:
    // el POST se difiere al comando 'cloud'.
    if (ESP.getFreeHeap() >= 45000) {
      enviarSupabase(true);           // configura el cliente TLS (setInsecure, ALPN)
      // Un "ini" pendiente de la sesión anterior hacía que el robot arrancara
      // solo al encenderlo. Se descartan los comandos viejos.
      descartarComandosPendientes();
    } else {
      Serial.println("Supabase: POST diferido (heap insuficiente con BT activo). Usa 'cloud 60'.");
    }
  }
#else
  Serial.println("WiFi DESACTIVADO (modo solo Bluetooth)");
#endif
}

/* Marca como procesados todos los comandos viejos.
   Sin esto, al encender el robot ejecuta el último "ini" que haya en la nube. */
void descartarComandosPendientes() {
  HTTPClient http;
  http.setTimeout(TIMEOUT_NUBE_MS);
  http.begin(supabaseClient,
             "https://vygwqsxyfdllirelndky.supabase.co/rest/v1/comandos_robot?procesado=is.false");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  int code = http.sendRequest("PATCH", "{\"procesado\":true}");
  Serial.printf("Comandos viejos descartados: %d\n", code);
  http.end();
}

/* =========================================================
   ==== COMANDOS (BT + USB) — parser NO bloqueante =========
   Se atienden al INICIO del loop y también DENTRO de cada
   espera, así "alto" responde de inmediato.
   ========================================================= */
String btBuf = "";
String usbBuf = "";

void responder(const String &texto, bool porBT) {
  if (porBT && SerialBT.hasClient()) SerialBT.println(texto);
  Serial.println(texto);
}

void imprimirDiagnostico() {
  logDiag("--- DIAG ---");
  logDiag("MQ2: DO=%s pol=%s alerta=%d corte=%s ppm=%.0f latch=%d",
          digitalRead(MQ2_PIN) == HIGH ? "HIGH" : "LOW",
          mq2NivelActivo == LOW ? "LOW" : "HIGH",
          (int)gas_alerta, gasCorteActivo ? "activo" : "liberado",
          mq2UltimoPPM, (int)mq2LatchedAlarma);
  logDiag("Dist: F=%.1f L=%.1f R=%.1f cm", d_front, d_left, d_right);
  logDiag("Pose: x=%.1f y=%.1f th=%.2f (cm/rad)", x * 100.0f, y * 100.0f, theta);
  logDiag("BT=%s WiFi=%s heap=%u up=%lu s",
          btConectado ? "conectado" : "libre",
          WiFi.status() == WL_CONNECTED ? "OK" : "sin",
          (unsigned)ESP.getFreeHeap(), millis() / 1000UL);
}

void ejecutarComando(const String &cmd, bool porBT) {
  String cmdLow = cmd;
  cmdLow.toLowerCase();
  if (cmd.equalsIgnoreCase("ini") || cmd.equalsIgnoreCase("start")) {
    if (!iniciarMovimiento) {
      // Arranque desde parado: el robot está quieto, así que se recalibra el
      // bias del giroscopio y se fija el rumbo actual como objetivo recto.
      M1_alto();
      M2_alto();
      calibrarGyroZ(500);
      estadoEvasion = EV_AVANZAR;
      escaneoIniciado = false;
      huecoReservado = 0.0f;
      thetaObjetivo = theta;
      integralGiro = 0.0f;
      bumperHit = false;
      prevLeft = encoderLeft;
      prevRight = encoderRight;
      estabaMoviendo = false;
      logDiag("INI: rumbo fijado en %.2f rad (PID recto)", thetaObjetivo);
    }
    iniciarMovimiento = true;
    responder("ACK:INI", porBT);
  } else if (cmd.equalsIgnoreCase("alto") || cmd.equalsIgnoreCase("stop")) {
    iniciarMovimiento = false;
    M1_alto();
    M2_alto();
    estadoEvasion = EV_AVANZAR;
    escaneoIniciado = false;
    estabaMoviendo = false;
    responder("ACK:ALTO", porBT);
  } else if (cmd.equalsIgnoreCase("ping")) {
    responder("ACK:PONG", porBT);
  } else if (cmd.equalsIgnoreCase("diag")) {
    imprimirDiagnostico();
    responder("ACK:DIAG", porBT);
  } else if (cmd.equalsIgnoreCase("net")) {
    // Prueba bloqueante (~20 s): se detiene el robot antes, por seguridad
    iniciarMovimiento = false;
    M1_alto();
    M2_alto();
    diagnosticoRed();
    enviarSupabase(true);
    responder("ACK:NET", porBT);
  } else if (cmd.equalsIgnoreCase("heap")) {
    responder(String("HEAP:") + (unsigned)ESP.getFreeHeap() +
              " max=" + (unsigned)ESP.getMaxAllocHeap(), porBT);
  } else if (cmd.equalsIgnoreCase("gasinv")) {
    mq2NivelActivo = (mq2NivelActivo == LOW) ? HIGH : LOW;
    gas_alerta = false;
    gas_valor = 0;
    mq2LatchedAlarma = false;
    tGasDesde = millis();
    responder(String("ACK:GASINV pol=") + (mq2NivelActivo == LOW ? "LOW" : "HIGH") + " latch=off", porBT);
  } else if (cmd.equalsIgnoreCase("gasoff")) {
    gasCorteActivo = false;
    // Unica forma de soltar el latch junto con "gascal": nunca se libera solo.
    gas_alerta = false;
    gas_valor = 0;
    mq2LatchedAlarma = false;
    tGasDesde = millis();
    responder("ACK:GASOFF corte desactivado y latch liberado (si aun hay gas volvera a marcar, pero no detendra)", porBT);
  } else if (cmd.equalsIgnoreCase("gascal")) {
    // Recalibrar en aire limpio: detiene el robot y vuelve a medir R0 y el DO.
    iniciarMovimiento = false;
    M1_alto();
    M2_alto();
    mq2Calibrado = false;
    mq2PolaridadOk = false;
    mq2ForzarPol = true;
    mq2ForzarR0 = true;
    mq2Confiable = false;
    gas_alerta = false;
    gas_valor = 0;
    mq2LatchedAlarma = false;
    gasCorteActivo = true;
    responder("ACK:GASCAL robot detenido; midiendo ~14 s en aire limpio (quedara mudo un rato)", porBT);
  } else if (cmd.equalsIgnoreCase("gason")) {
    gasCorteActivo = true;
    tGasDesde = millis();
    responder("ACK:GASON corte por gas activado", porBT);
  } else if (cmdLow.startsWith("cloud") || cmdLow.startsWith("nube")) {
    int seg = 60;
    int sp = cmdLow.indexOf(' ');
    if (sp > 0) {
      seg = cmdLow.substring(sp + 1).toInt();
      if (seg <= 0) {
        seg = 60;
      }
    }
    responder(String("ACK:CLOUD ") + seg + "s (cierro BT; la app debe pasar a modo WiFi)", porBT);
    entrarModoCloud(seg);
  } else if (cmd.equalsIgnoreCase("mq2")) {
    /* Diagnóstico del MQ-2: dice el nivel del DO, el voltaje del AO (para
       confirmar si el cable AO->GPIO36 está conectado), el R0 y el estado de
       la alarma. Con esto se sabe de un vistazo si el sensor sirve. */
    float vAO = leerMQ2voltios();
    responder(String("MQ2: DO=") + (digitalRead(MQ2_PIN) == HIGH ? "HIGH" : "LOW") +
              " pol=" + (mq2NivelActivo == LOW ? "LOW" : "HIGH") +
              " alerta=" + (int)gas_alerta +
              " latch=" + (int)mq2LatchedAlarma +
              " AO=" + String(vAO, 2) + "V" +
              (vAO <= 0.05f ? " (SIN SEÑAL: cable AO->GPIO36 desconectado)" : " (AO OK)") +
              " ppm=" + String(mq2UltimoPPM, 0) +
              " R0=" + String(mq2R0, 1) + "k" +
              " DO_util=" + (int)mq2Confiable, porBT);
  } else if (cmd.equalsIgnoreCase("mq2test")) {
    /* Prueba manual de gas: lee 20 veces el DO y el AO durante 4 s y muestra
       la variación. Con el encendedor (sin llama) cerca se ve cómo suben los
       ppm y cómo baja el DO. No cambia ninguna alarma. */
    logDiag("MQ2 test: 4 s leyendo DO y AO (acerca el encendedor sin llama)");
    for (int i = 0; i < 8; i++) {
      float v = leerMQ2voltios();
      logDiag("  DO=%s AO=%.3f V ppm=%.0f",
              digitalRead(MQ2_PIN) == HIGH ? "HIGH" : "LOW",
              v, (v <= 0.05f) ? -1.0f : (1000.0f * pow((MQ2_RL * (3.3f - v) / v) / mq2R0, -2.3f)));
      delay(500);
    }
    responder("ACK:MQ2TEST 8 muestras tomadas", porBT);
  } else {
    responder(String("ERR:CMD?") + cmd, porBT);
  }
}

// Lee los bytes disponibles SIN bloquear (no usa readStringUntil)
void atenderComandos() {
  while (SerialBT.available()) {
    char c = (char)SerialBT.read();
    if (c == '\n' || c == '\r') {
      String cmd = btBuf;
      btBuf = "";
      cmd.trim();
      if (cmd.length()) ejecutarComando(cmd, true);
    } else if (btBuf.length() < 32) {
      btBuf += c;
    } else {
      btBuf = "";   // basura: se descarta
    }
  }
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      String cmd = usbBuf;
      usbBuf = "";
      cmd.trim();
      if (cmd.length()) ejecutarComando(cmd, false);
    } else if (usbBuf.length() < 32) {
      usbBuf += c;
    } else {
      usbBuf = "";
    }
  }
}

/* Espera que sigue atendiendo comandos.
   Devuelve false si durante la espera llegó "alto". */
bool esperar(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    atenderComandos();
    enviarPosicion();          // telemetría también durante las maniobras
    if (!iniciarMovimiento) {
      M1_alto();
      M2_alto();
      return false;
    }
    delay(5);
  }
  return true;
}

/* ================= MODO LIBRE (evita obstáculos) ================= */
/* ================= NAVEGACION =================
   El robot avanza recto con un PID de rumbo sobre el giroscopio. Cuando algo
   se acerca a DIST_ESCANEAR (50 cm) NO traza una curva ni pivota 90 grados a
   ciegas: se detiene, gira sobre su propio eje dando la vuelta completa en 8
   paradas de 45 grados midiendo el frente en cada una, y despues EV_GIRAR lo
   lleva al rumbo mas despejado que encontro. Si la vuelta entera esta cerrada,
   retrocede y da media vuelta. El bumper queda solo como emergencia. */

float normalizarAngulo(float a) {
  while (a > PI)  a -= 2.0f * PI;
  while (a < -PI) a += 2.0f * PI;
  return a;
}

void parar() {
  M1_alto();
  M2_alto();
}

/* Pivote sobre el centro: una rueda adelante y la otra atrás.
   dir > 0 = izquierda (theta aumenta, convención matemática). */
void pivote(int dir, int v) {
  if (g_pivoteInvertido) dir = -dir;
  if (dir > 0) { M1_adelante(v); M2_reversa(v); }
  else         { M1_reversa(v); M2_adelante(v); }
}

/* Lectura lateral de una muestra. El HC-SR04 tiene un mínimo de ~2-3 cm:
   por debajo de eso es ruido o el eco de otro sensor (crosstalk). */
float medirLado(int trig, int echo) {
  float d = medis(trig, echo);
  if (d > 0.0f && d < 3.0f) return 0.0f;
  return d;
}

/* Autocomprobacion del sentido de giro: una sola vez en la vida del robot.
   pivote(+1) debe hacer crecer theta. Si las ruedas estan al reves se marca
   g_pivoteInvertido y pivote() lo compensa desde entonces. Sin esto el barrido
   360 giraria al reves de lo supuesto y agotaria el timeout en cada paso
   (8 x 3.5 s girando en el sitio sin medir nada util). */
void comprobarSentidoPivote() {
  pivote(+1, VEL_PIVOTE);
  if (!esperar(150)) return;
  float g = 0.0f;
  for (int i = 0; i < 5; i++) { leerGyroZ(); g += gyroZ_rads; delay(8); }
  g /= 5.0f;
  parar();
  if (g > 0.20f) {
    g_signoPivoteConocido = true;
    logDiag("Pivote: sentido IZQUIERDA confirmado (gyro=%.2f)", g);
  } else if (g < -0.20f) {
    g_pivoteInvertido = true;
    g_signoPivoteConocido = true;
    logDiag("Pivote: ruedas invertidas -> corregido (gyro=%.2f)", g);
  } else {
    logDiag("Pivote: no gira (gyro=%.2f), se reintenta", g);
  }
}

/* Lectura del frente con filtro de crosstalk.
   Los tres HC-SR04 estan montados cerca: el sensor lateral puede "oir" el
   pulso del frontal y reportar 4-6 cm, que es un falso positivo. Con DOS
   muestras separadas, si una sale clarisima corta y la otra larga, es ruido:
   se descarta y se devuelve la otra. Si las dos son cortas, hay obstaculo de
   verdad (o el robot va pegado a la pared). */
float leerFrenteFiltrado() {
  float a = medis(TrigF, EchoF);
  delay(12);
  float b = medis(TrigF, EchoF);
  delay(12);

  if (a <= 0.0f) return b;
  if (b <= 0.0f) return a;

  // Una corta (<8 cm) y otra larga (>15 cm): es crosstalk, se queda la larga.
  if (a < 8.0f && b > 15.0f) return b;
  if (b < 8.0f && a > 15.0f) return a;

  return (a < b) ? a : b;   // la menor: frenado conservador
}

/* Zero-rate update: en reposo el giroscopio deberia dar 0, pero su bias se
   desplaza con la temperatura y el calor. Sin corregirlo, theta se va sola y el
   PID lo sigue como si el robot curvara -> el robot "da vueltas" solo.
   Cuando el robot lleva quieto un rato se realimenta el offset con el
   residuo medido (alpha chica para no responder a un giro real). */
void actualizarBiasGyroEnReposo() {
  static unsigned long tZRU = 0;
  if (iniciarMovimiento) { tZRU = millis(); return; }
  if (millis() - tZRU < 500UL) return;
  tZRU = millis();

  float g = 0.0f;
  for (int i = 0; i < 5; i++) { leerGyroZ(); g += gyroZ_rads; delay(8); }
  g /= 5.0f;
  // Solo si el residuo es pequeno: si el robot esta girando de verdad, no tocar.
  if (fabs(g) < 0.15f) {
    gyroZ_offset += g * 0.05f;
  }
}

/* Rumbo recto: PID + ayuda de encoders.
   u > 0 hace girar a la IZQUIERDA (rueda derecha mas rapida). */
void avanzarRectoPID(int velBase, float dt) {
  float error = normalizarAngulo(thetaObjetivo - theta);
  integralGiro += error * dt;
  integralGiro = constrain(integralGiro, -0.6f, 0.6f);

  // P + I + D(gyro). El término D usa la velocidad angular medida: frena la
  // corrección antes de pasarse, que es lo que hacía serpentear al robot.
  float u = KP_GIRO * error + KI_GIRO * integralGiro - KD_GIRO * gyroZ_rads;

  // Ayuda de encoders: si la rueda izquierda avanzó más, el robot se está
  // yendo a la derecha; se corrige sin esperar a que el gyro acumule error.
  float difEnc = (float)(g_deltaL - g_deltaR);
  u += constrain(difEnc * 1.5f, -20.0f, 20.0f);

  int velDer = (int)(velBase + u);
  int velIzq = (int)(velBase - u);

  // Si una rueda supera el máximo, se bajan las dos conservando la diferencia.
  int tope = max(velDer, velIzq);
  if (tope > vel_max) {
    int exceso = tope - vel_max;
    velDer -= exceso;
    velIzq -= exceso;
  }
  velDer = constrain(velDer, 45, vel_max);
  velIzq = constrain(velIzq, 45, vel_max);

  M1_adelante(velDer);
  M2_adelante(velIzq);
}

void pasoMovimiento() {
  digitalWrite(LED_PIN, HIGH);

  // Corte por gas: la única parada normal permitida.
  if (gas_alerta && gasCorteActivo) {
    parar();
    estabaMoviendo = false;
    digitalWrite(LED_PIN, LOW);
    return;
  }

  if (!estabaMoviendo) {          // arranque limpio de odometría
    estabaMoviendo = true;
    prevLeft = encoderLeft;
    prevRight = encoderRight;
    t_odo_ms = millis();
  }

  leerGyroZ();

  long currentLeft = encoderLeft;
  long currentRight = encoderRight;
  g_deltaL = currentLeft - prevLeft;
  g_deltaR = currentRight - prevRight;
  prevLeft = currentLeft;
  prevRight = currentRight;

  unsigned long now_ms = millis();
  float dt = (now_ms - t_odo_ms) / 1000.0f;
  if (dt <= 0.0f || dt > 0.5f) dt = 0.05f;
  t_odo_ms = now_ms;

  // Rumbo SOLO con giroscopio: los encoders de un canal no distinguen el
  // sentido de giro y en un pivote darían un ángulo falso.
  float thetaAntes = theta;
  theta = normalizarAngulo(theta + gyroZ_rads * dt);
  float thetaMid = normalizarAngulo(thetaAntes + gyroZ_rads * dt * 0.5f);

  // Traslación solo avanzando de frente (en reversa/pivote los encoders
  // cuentan igual y ensuciarían el mapa).
  if (estadoEvasion == EV_AVANZAR) {
    float dC = (g_deltaL + g_deltaR) * 0.5f * distancia_pulso;
    x += dC * cos(thetaMid);
    y += dC * sin(thetaMid);
  }

  switch (estadoEvasion) {

    case EV_AVANZAR: {
      // Un sensor a la vez y con pausa: dos HC-SR04 disparando juntos se oyen
      // entre sí y el vecino reporta 4-6 cm (crosstalk).
      d_front = leerFrenteFiltrado();
      static uint8_t turnoLado = 0;
      if (turnoLado == 0) d_left  = medirLado(TrigL, EchoL);
      else                d_right = medirLado(TrigR, EchoR);
      turnoLado = (uint8_t)((turnoLado + 1) % 2);
      delay(10);

      /* Gatillo a DIST_ESCANEAR (50 cm, antes 30): reacciona antes de estar
         encima del obstaculo, que es cuando girar en el sitio todavia tiene
         espacio. El bumper queda solo como emergencia.
         Hueco reservado: si el barrido 360 eligio un hueco de 46 cm (entre
         DIST_CLEAR 45 y DIST_ESCANEAR 50), el frente sigue marcando 46 al
         terminar y el gatillo re-dispara el barrido: el robot barre en bucle
         sin avanzar nunca. Durante la ventana posterior a la evasion el umbral
         baja a la medida real del hueco; fuera de ella vuelve a 50 cm, que es
         lo que corresponde si el hueco ya se Alejo o si el robot cambio de
         rumbo con otro barrido. */
      bool enVentana = (millis() - tFinEvasion) < (GRACIA_ESCANEO_MS + 2500UL);
      float umbral = DIST_ESCANEAR;
      if (enVentana && huecoReservado > 0.0f && huecoReservado < DIST_ESCANEAR) {
        umbral = huecoReservado;
      }
      bool porDistancia = (d_front > 0.0f && d_front < umbral) &&
                          (millis() - tFinEvasion > GRACIA_ESCANEO_MS);
      if (bumperHit || porDistancia) {
        bool golpe = bumperHit;
        bumperHit = false;
        parar();
        if (!esperar(120)) return;
        logDiag("Evasion: %s F=%.1f (umbral %.0f) -> barrido 360",
                golpe ? "GOLPE" : "obstaculo", d_front, umbral);
        escaneoIniciado = false;
        huecoReservado = 0.0f;   // empieza un barrido nuevo: hueco por elegir
        tEvasion = millis();
        estadoEvasion = EV_ESCANEAR;
        break;
      }

      int velBase = (d_front <= 0.0f || d_front >= d_max)
                    ? vel_max
                    : velocidadProporcional(d_front);
      avanzarRectoPID(velBase, dt);
      if (!esperar(70)) return;
      break;
    }

    /* BARRIDO 360: 8 paradas de 45 grados midiendo el frente en cada una.
       Sustituye al escaneo grueso frente/izquierda/derecha, que a veces
       terminaba mirando otra pared: aqui se recorre la vuelta completa y se
       guarda el rumbo mas despejado que se vio, y al final EV_GIRAR lleva al
       robot justo a ese rumbo. */
    case EV_ESCANEAR: {
      // Sin el sentido de giro confirmado el barrido giraria al reves.
      if (!g_signoPivoteConocido) {
        comprobarSentidoPivote();
        break;
      }

      /* El arranque se hace aqui y no en la transicion desde EV_AVANZAR para
         que la autocomprobacion de arriba (que gira el robot de verdad) no
         ensucie el angulo de partida del barrido. */
      if (!escaneoIniciado) {
        escaneoIniciado    = true;
        escaneoPaso        = 0;
        escaneoMejorDist   = 0.0f;
        escaneoThetaInicio = theta;
        anguloGiro = normalizarAngulo(theta + ANG_PASO);
        tEvasion = millis();
        logDiag("Escaneo 360: %d pasos de %.0f grados desde th=%.2f",
                (int)PASOS_ESCANEO, ANG_PASO * RAD_TO_DEG, escaneoThetaInicio);
        break;
      }

      // Girar al angulo del paso. Lento (VEL_ESCANEO) para que el gyro no se
      // pase de largo y la lectura se asiente.
      float error = normalizarAngulo(anguloGiro - theta);
      if (fabs(error) > TOL_ANGULO && millis() - tEvasion < TIMEOUT_GIRO_MS) {
        pivote(error > 0.0f ? +1 : -1, VEL_ESCANEO);
        if (!esperar(35)) return;
        break;
      }

      parar();
      if (!esperar(150)) return;   // quieto antes de medir: el HC-SR04 mide
                                   // mal con el robot vibrando
      float crudo = leerSensorMediana(TrigF, EchoF);
      d_front = crudo;                          // telemetria viva durante el barrido
      float f = (crudo <= 0.0f) ? 300.0f : crudo;   // sin eco = abierto

      // Se guarda el theta REAL, no el objetivo: si un paso se fue por timeout
      // la medida sigue asociada al rumbo en el que de verdad quedo el robot.
      if (f > escaneoMejorDist) {
        escaneoMejorDist   = f;
        escaneoMejorAngulo = theta;
      }
      escaneoPaso++;
      logDiag("Escaneo %d/%d th=%.2f F=%.0f | mejor=%.0f",
              (int)escaneoPaso, (int)PASOS_ESCANEO, theta, f, escaneoMejorDist);

      if (escaneoPaso >= PASOS_ESCANEO) {
        escaneoIniciado = false;
        if (escaneoMejorDist >= DIST_CLEAR) {
          anguloGiro = escaneoMejorAngulo;   // ir al mejor rumbo encontrado
          tEvasion = millis();
          /* Se reserva el hueco elegido: el gatillo de barrido baja hasta
             esta medida para no re-barrar en bucle. */
          huecoReservado = escaneoMejorDist;
          estadoEvasion = EV_GIRAR;
          logDiag("Escaneo 360: mejor rumbo th=%.2f (%.0f cm) -> girar",
                  escaneoMejorAngulo, escaneoMejorDist);
        } else {
          tEvasion = millis();
          huecoReservado = 0.0f;
          estadoEvasion = EV_RETROCEDER;     // todo cerrado: retroceder
          logDiag("Escaneo 360: nada despejado (mejor %.0f cm) -> retroceder",
                  escaneoMejorDist);
        }
        break;
      }

      anguloGiro = normalizarAngulo(escaneoThetaInicio + (escaneoPaso + 1) * ANG_PASO);
      tEvasion = millis();
      break;
    }

    case EV_GIRAR: {
      // Primera vez: verificar con el gyro que las ruedas giran en el sentido
      // esperado. Si estan al reves, se invierte el comando y se autocorrige.
      if (!g_signoPivoteConocido) {
        comprobarSentidoPivote();
        break;
      }

      float error = normalizarAngulo(anguloGiro - theta);
      if (fabs(error) <= TOL_ANGULO || millis() - tEvasion > TIMEOUT_GIRO_MS) {
        parar();
        if (!esperar(120)) return;
        logDiag("Evasion: giro listo th=%.2f obj=%.2f err=%.2f",
                theta, anguloGiro, error);
        /* Ya estamos en el rumbo elegido (el mejor del barrido, o la media
           vuelta tras retroceder): a avanzar recto por el. Si vuelve a haber
           algo cerca, EV_AVANZAR rearma otro barrido desde el rumbo nuevo. */
        thetaObjetivo = theta;
        integralGiro = 0.0f;
        tFinEvasion = millis();
        /* El hueco reservado NO se limpia aqui: sigue sirviendo durante la
           ventana posterior a la evasion para que el frente del hueco elegido
           (46 cm, por ejemplo) no re-dispare el barrido al instante. */
        estadoEvasion = EV_AVANZAR;
        break;
      }

      int v = VEL_PIVOTE;
      if (fabs(error) < 0.30f) v = 115;      // frenar al acercarse
      pivote(error > 0.0f ? +1 : -1, v);
      if (!esperar(35)) return;
      break;
    }

    case EV_RETROCEDER: {
      if (millis() - tEvasion < 800UL) {
        M1_reversa(vel3);
        M2_reversa(vel3);
        if (!esperar(60)) return;
      } else {
        parar();
        if (!esperar(150)) return;
        anguloGiro = normalizarAngulo(theta + PI);   // media vuelta
        tEvasion = millis();
        escaneoIniciado = false;
        estadoEvasion = EV_GIRAR;
        logDiag("Evasion: retroceso terminado -> media vuelta");
      }
      break;
    }
  }

  digitalWrite(LED_PIN, LOW);
}

/* Pulsadores de colisión: solo marcan el evento, con antirrebote.
   La maniobra la resuelve pasoMovimiento() con los ultrasonidos.
   Antes cada pulsador hacía su propia secuencia de tiempos fijos y el
   robot quedaba "dando saltitos" en vez de esquivar. */
void revisarColisiones() {
  unsigned long ahora = millis();
  if (ahora - tUltimoBumper < 1200UL) return;   // no repetir por rebote

  // Confirmar 3 lecturas seguidas: los PB hacen ruido al vibrar.
  bool f = true, l = true, r = true;
  for (int i = 0; i < 3; i++) {
    if (!bFval || digitalRead(bF) != LOW) f = false;
    if (!bLval || digitalRead(bL) != LOW) l = false;
    if (!bRval || digitalRead(bR) != LOW) r = false;
    delay(2);
  }
  if (!f && !l && !r) return;

  if (f) choqueF++;
  if (l) choqueL++;
  if (r) choqueR++;

  tUltimoBumper = ahora;
  bumperHit = true;
  logDiag("[Bumper] F=%d L=%d R=%d", (int)f, (int)l, (int)r);
}

void loop() {
  // === 1. COMANDOS PRIMERO (respuesta inmediata) ===
  atenderComandos();

  leerMQ2();
  calibrarPolaridadMQ2();
  calibrarMQ2R0();
  actualizarBiasGyroEnReposo();

  // === ESTADO DEL ENLACE BLUETOOTH ===
  bool clienteBT = SerialBT.hasClient();
  if (clienteBT != btConectado) {
    btConectado = clienteBT;
    if (clienteBT) {
      Serial.println("BT: cliente conectado");
      SerialBT.println("Robot listo (ini | alto | ping | diag | net | cloud)");
      // Al conectar, si la nube falló, se manda el diagnóstico solo:
      // así el "Registro del enlace" de la app muestra la causa sin USB.
      if (ultimoPostSupabase < 0 && !iniciarMovimiento) {
        tAutoDiag = millis() + 4000UL;
      }
    } else {
      Serial.println("BT: cliente desconectado");
      iniciarMovimiento = false;   // seguridad: sin enlace no se mueve
      M1_alto();
      M2_alto();
      estabaMoviendo = false;
    }
  }

  // Diagnóstico automático al conectar (solo si el robot está detenido)
  if (tAutoDiag > 0 && millis() >= tAutoDiag) {
    tAutoDiag = 0;
    logDiag("--- DIAGNOSTICO AUTOMATICO (el POST a Supabase fallo) ---");
    diagnosticoRed();
  }

  // Estado compacto cada 20 s: visible en el registro de la app
  static unsigned long tEstado = 0;
  if (millis() - tEstado > 20000) {
    tEstado = millis();
    lineaEstado();
  }

  // === LATIDO (detecta reinicios por consumo de batería) ===
  if (millis() - tHeartbeat > 5000) {
    tHeartbeat = millis();
    Serial.printf("[vivo] %lu s | BT=%s | WiFi=%s | gas=%d | ini=%d\n",
                  millis() / 1000,
                  btConectado ? "conectado" : "libre",
                  WiFi.status() == WL_CONNECTED ? "OK" : "sin",
                  gas_valor, (int)iniciarMovimiento);
  }

#if USAR_WIFI
  // === RECONEXION WiFi (cada 30 s si se cae) ===
  static unsigned long tReWiFi = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - tReWiFi > 30000) {
    tReWiFi = millis();
    Serial.println("WiFi: reintentando conexion...");
    WiFi.reconnect();
  }
#endif

  // === 2. MOVIMIENTO ===
  if (iniciarMovimiento) {
    pasoMovimiento();
    // tras el paso pudo llegar "alto" o activarse el gas: no se maniobra más
    if (iniciarMovimiento && !(gas_alerta && gasCorteActivo)) revisarColisiones();
  } else {
    estabaMoviendo = false;
    M1_alto();
    M2_alto();
    digitalWrite(LED_PIN, LOW);

    // Sensores "en reposo": la app ve el entorno aunque el robot esté parado
    static unsigned long tSensores = 0;
    if (millis() - tSensores > INTERVALO_SENSORES_REPOSO) {
      tSensores = millis();
      d_front = medis(TrigF, EchoF);
      d_left  = medis(TrigL, EchoL);
      d_right = medis(TrigR, EchoR);
    }
  }

  // === 3. TELEMETRÍA SIEMPRE (5 Hz): mapa vivo aunque esté detenido ===
  enviarPosicion();

  // === 4. NUBE: envío de datos y comandos por WiFi ===
#if USAR_WIFI
  // Auto-cloud: si el heap no alcanza para TLS con BT activo, abre ventana cloud automática
  if (!modoCloud && ESP.getFreeHeap() < 45000 && millis() - tUltimoAutoCloud > 30000UL) {
    tUltimoAutoCloud = millis();
    entrarModoCloud(5);
  }
  // El POST bloquea ~200-400 ms. Se prefiere lanzarlo con espacio libre al
  // frente para no avanzar a ciegas, pero NUNCA se deja la nube muda: si el
  // robot lleva 8 s cerca de una pared, publica igual (la app aguanta 15 s).
  static unsigned long tUltimoPostNube = 0;
  bool espacioAlFrente = (d_front <= 0) || (d_front > 35.0f);
  bool ventanaPost = !iniciarMovimiento || espacioAlFrente ||
                     (millis() - tUltimoPostNube > 8000UL);
  if (ventanaPost && millis() - ultimoEnvioSupabase >= INTERVALO_SUPABASE) {
    enviarSupabase(false);
    tUltimoPostNube = millis();
  }

  // Los comandos se revisan SIEMPRE. Antes quedaban bloqueados cuando el
  // frente estaba a menos de 35 cm y "Detener" no llegaba nunca.
  revisarComandos();
  if (modoCloud && millis() >= tCloudHasta) salirModoCloud();
#endif
}

// === Velocidad proporcional según distancia ===
int velocidadProporcional(float distancia) {
  if (distancia >= d_max) return vel_max;
  if (distancia <= d_min) return vel_min;
  float k = (vel_max - vel_min) / (d_max - d_min);
  return vel_min + k * (distancia - d_min);
}

void enviarPosicion() {
  // Límite a 5 Hz para no saturar el puerto (Python lee a 10 Hz)
  static unsigned long ultimoEnvio = 0;
  if (millis() - ultimoEnvio < 200) return;
  ultimoEnvio = millis();

  String gasTxt = gas_alerta ? "true" : "false";
  String msg = "{\"pose\":{\"x\":" + String(x, 2) + ",\"y\":" + String(y, 2) + ",\"theta\":" + String(theta, 2) + "},\"sensors\":{\"front\":" + String(d_front, 1) + ",\"left\":" + String(d_left, 1) + ",\"right\":" + String(d_right, 1) + ",\"gas\":" + String(gas_valor) + ",\"gas_alerta\":" + gasTxt + ",\"ppm\":" + String(mq2UltimoPPM, 1) + ",\"latch\":" + String((int)mq2LatchedAlarma) + "},\"mov\":" + String((int)iniciarMovimiento) + ",\"timestamp\":" + String(millis()) + "}";
  SerialBT.println(msg);
  Serial.println(msg);  // respaldo por USB (COM5) para verificar telemetría
}

// === Control de motores ===
void M1_adelante(int x) {
  digitalWrite(IN1M1, HIGH); digitalWrite(IN2M1, LOW);
  ledcWrite(enAM1, x);
}
void M1_reversa(int x) {
  digitalWrite(IN1M1, LOW); digitalWrite(IN2M1, HIGH);
  ledcWrite(enAM1, x);
}
void M1_alto() {
  digitalWrite(IN1M1, LOW); digitalWrite(IN2M1, LOW);
  ledcWrite(enAM1, 0);
}
void M2_adelante(int y) {
  digitalWrite(IN3M2, HIGH); digitalWrite(IN4M2, LOW);
  ledcWrite(enBM2, y);
}
void M2_reversa(int y) {
  digitalWrite(IN3M2, LOW); digitalWrite(IN4M2, HIGH);
  ledcWrite(enBM2, y);
}
void M2_alto() {
  digitalWrite(IN3M2, LOW); digitalWrite(IN4M2, LOW);
  ledcWrite(enBM2, 0);
}

// Función para medir distancias
float medis(int triggerPin, int echoPin) {
  // Si la línea de eco quedó HIGH de una lectura anterior (timeout),
  // pulseIn se pierde el pulso nuevo y devuelve 0. Se espera a que baje.
  unsigned long tEspera = micros();
  while (digitalRead(echoPin) == HIGH) {
    if (micros() - tEspera > 5000UL) break;
  }

  digitalWrite(triggerPin, LOW); delayMicroseconds(3);
  digitalWrite(triggerPin, HIGH); delayMicroseconds(10);
  digitalWrite(triggerPin, LOW);
  // Timeout 30 ms (~5 m): si no hay eco NO se queda 1 s esperando
  unsigned long duracion = pulseIn(echoPin, HIGH, 30000UL);
  if (duracion == 0) return 0.0;   // sin eco: se descarta en el filtro
  return (duracion * 0.0343 / 2);
}

// Función para leer un sensor con filtro de mediana
float leerSensorMediana(int triggerPin, int echoPin) {
  float validas[N];
  int n = 0;

  // Tomar N lecturas consecutivas, descartando los ecos perdidos (0).
  // Un eco perdido NO debe confundirse con un obstáculo pegado al sensor.
  for (int i = 0; i < N; i++) {
    float d = medis(triggerPin, echoPin);
    if (d > 0.0) validas[n++] = d;
    delay(4); // rápido: 3 muestras por sensor
  }

  if (n == 0) return 0.0;   // sin eco: el llamador decide qué significa

  // Ordenar las lecturas válidas (burbuja simple)
  for (int i = 0; i < n-1; i++) {
    for (int j = i+1; j < n; j++) {
      if (validas[j] < validas[i]) {
        float temp = validas[i];
        validas[i] = validas[j];
        validas[j] = temp;
      }
    }
  }

  // Devolver la mediana
  return validas[n/2];
}


/* =========================
   ========= IMU ===========
   ========================= */
void initMPU() {
  // Despertar
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission(true);
  // Gyro ±500 dps: FS_SEL=1 (0x1B = 0b00001000)
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1B); Wire.write(0x08); Wire.endTransmission(true);
  // (Opcional) LPF en acelerómetro/gyro si se usan más ejes
}

void calibrarGyroZ(int N) {
  long sum = 0;
  int validas = 0;
  int fallos = 0;
  const float SENS = 65.5f; // ±500 dps
  for (int i = 0; i < N; i++) {
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x47); Wire.endTransmission(false);
    if (Wire.requestFrom(MPU_ADDR, (uint8_t)2, (bool)true) == 2) {
      fallos = 0;
      int16_t gz_raw = (Wire.read() << 8) | Wire.read();
      sum += gz_raw;
      validas++;
    } else if (++fallos >= 20) {
      // 20 fallos seguidos = no hay IMU. No perder 38 s reintentando.
      break;
    }
    delay(1);
  }
  if (validas == 0) {
    gyroZ_offset = 0.0f;
    Serial.println("IMU NO RESPONDE: calibracion omitida (solo encoders)");
    return;
  }
  float gz_raw_bias = sum / (float)validas;
  float gz_dps_bias = gz_raw_bias / SENS;
  gyroZ_offset = gz_dps_bias * DEG_TO_RAD; // rad/s
  Serial.printf("IMU OK: %d/%d muestras\n", validas, N);
}

void leerGyroZ() {
  // GYRO_ZOUT_H/L
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x47); Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, (uint8_t)2, (bool)true);
  if (Wire.available() == 2) {
    int16_t gz_raw = (Wire.read() << 8) | Wire.read();
    const float SENS = 65.5f;                // ±500 dps
    float gz_dps  = gz_raw / SENS;           // °/s
    float gz_rads = gz_dps * DEG_TO_RAD;     // rad/s
    gyroZ_rads = gz_rads - gyroZ_offset;     // compensa bias
  }
}


