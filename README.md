# Robot-Gas

**Etapa de comunicación WiFi para un vehículo terrestre autónomo**

Robot autónomo con ESP32 que detecta gas (MQ-2), se mueve solo evitando
obstáculos y reporta su estado en tiempo real a una app Android por WiFi
(a través de Supabase).

- **Estudiante:** Angel Armando Carreño González
- **Investigadora:** Alba Maribel Sánchez Gálvez
- **Institución:** BUAP — Facultad de Ciencias Físico-Matemáticas

---

## Qué hace

| Función | Detalle |
|---|---|
| Navegación | Avance recto con PID de rumbo sobre el giroscopio (MPU6050) |
| Evasión | Al detectar obstáculo se detiene y hace un **barrido de 360°** (8 pasos de 45°), midiendo el frente en cada parada, y sale por el rumbo más despejado |
| Detección de gas | MQ-2 con lectura **analógica (AO)** para concentración en ppm + **DO** digital como segunda alarma, con histéresis y latch |
| Comunicación | Teletría a Supabase por HTTPS (TLS) y comandos desde la app |
| App Android | React Native: mapa en vivo, sensores, alerta de gas con parpadeo |

## Hardware

| Componente | Detalle |
|---|---|
| ESP32 | DevKit v1 (30 pines) |
| Puentes H | L298N (2 motores) |
| Motores | 2 x DC con encoder de un canal |
| IMU | MPU6050 (I2C, addr 0x68) |
| Ultrasónicos | 3 x HC-SR04 (frente / izquierda / derecha) |
| Sensor de gas | MQ-2 (DO en GPIO23, AO en GPIO36) |
| Bumpers | 3 x pulsadores (frente / izquierda / derecha) |
| Batería | 2 x 18650 en serie (7.4 V) |

### Conexiones

```
MQ-2    VCC -> 5V      GND -> GND
        DO   -> GPIO23  (con colector abierto: necesita pull-up)
        AO   -> GPIO36  (ADC1_CH0; el ADC2 no funciona con WiFi)

HC-SR04 frontal    TRIG -> GPIO5    ECHO -> GPIO18
HC-SR04 izquierdo  TRIG -> GPIO17   ECHO -> GPIO16
HC-SR04 derecho    TRIG -> GPIO25   ECHO -> GPIO34

L298N  ENA -> GPIO4 (motor derecho)   ENB -> GPIO15 (motor izquierdo)
        IN1 -> GPIO12  IN2 -> GPIO14  (derecho)
        IN3 -> GPIO33  IN4 -> GPIO32  (izquierdo)

MPU6050  SDA -> GPIO21   SCL -> GPIO22
Bumpers  frente -> GPIO26   izquierdo -> GPIO35   derecho -> GPIO27
```

## Estructura

```
firmware/RMA_3S_Mapa/     Sketch de Arduino (ESP32)
  RMA_3S_Mapa.ino         Firmware completo
  secrets.example.h       Plantilla de credenciales (copiar a secrets.h)
app/                      App Android (React Native)
  src/screens/            Dashboard y conexión
  src/components/         Medidor de gas y mapa
  src/hooks/              Datos en vivo (WiFi/Supabase + Bluetooth)
  android/                Proyecto Android nativo
docs/                     Esquema SQL y notas
```

## Puesta en marcha

### 1. Firmware

1. Copia la plantilla de secretos:
   ```bash
   cp firmware/RMA_3S_Mapa/secrets.example.h firmware/RMA_3S_Mapa/secrets.h
   ```
   y rellena WiFi y Supabase. `secrets.h` está en `.gitignore`, nunca se sube.
2. Compila con **Arduino CLI**:
   ```bash
   arduino-cli compile \
     --fqbn 'esp32:esp32:esp32:PartitionScheme=huge_app,DebugLevel=error' \
     --build-property 'compiler.optimization_flags=-Os' \
     'firmware/RMA_3S_Mapa'
   ```
3. Sube al ESP32:
   ```bash
   arduino-cli upload -p COM5 \
     --fqbn 'esp32:esp32:esp32:PartitionScheme=huge_app,DebugLevel=error' \
     --input-dir <build-path> 'firmware/RMA_3S_Mapa'
   ```

### 2. App

```bash
cd app
npm install
npx react-native run-android
```

Para el APK de tablet (solo ARM):

```bash
cd app/android
./gradlew assembleRelease "-PreactNativeArchitectures=armeabi-v7a,arm64-v8a"
```

### 3. Base de datos

Ejecuta `docs/supabase_schema.sql` en el editor SQL de Supabase. Crea las
tablas `lecturas_robot` y `comandos_robot` y las publica en `supabase_realtime`.

## Comandos del robot

Se envían por USB o por la tabla `comandos_robot` de Supabase.

| Comando | Efecto |
|---|---|
| `ini` / `start` | Arranca el movimiento (recalibra el giroscopio y fija el rumbo) |
| `alto` / `stop` | Detiene el robot |
| `ping` | Responde `ACK:PONG` |
| `diag` | Diagnóstico completo |
| `net` | Prueba de red (DNS/TCP/HTTP/TLS) |
| `heap` | Memoria libre del ESP32 |
| `gascal` | Recalibra el MQ-2 en aire limpio (detiene el robot ~14 s) |
| `gasinv` | Invierte la polaridad del DO y libera el latch |
| `gasoff` | Desactiva el corte por gas y libera el latch |
| `gason` | Reactiva el corte por gas |
| `mq2` | Estado del sensor: DO, voltaje del AO, ppm, R0 |
| `mq2test` | 8 lecturas DO+AO en 4 s (para probar con el encendedor) |

## Notas sobre el sensor de gas

El **MQ-2 es un comparador, no un sensor de concentración**: su salida digital
DO solo cambia cuando se cruza el umbral que fija el trimpot. Por eso la alarma
principal usa la salida **analógica AO**, que sí da concentración en ppm.

- La alarma tiene **histéresis** (300 ppm enciende / 200 ppm apaga) y **latch**:
  una vez que se enclava, el robot **no** se reanuda solo. Solo lo liberan
  `gasoff` o `gascal`.
- El **DO es de colector abierto**: necesita pull-up. No lo quites del pin, o
  queda flotante y lee LOW siempre (falso positivo de gas).
- El módulo necesita **3 minutos de calentamiento** antes de que el AO sea
  confiable. El DO responde a los ~20 s.
- Detecta **GLP, propano, metano, humo y alcohol** — no CO₂. El GLP pesa más
  que el aire: pon el sensor **bajo** y cerca de la fuente.
- Para probar: **encendedor con gas, sin llama**, pegado al sensor.

## Configuración del ESP32

El modo WiFi puro es el que se usa en producción (el Bluetooth se desactiva
para liberar ~95 kB de RAM, que es lo que necesita el TLS):

```cpp
#define USAR_WIFI 1
#define USAR_BLUETOOTH 0
```

## Licencia

Proyecto académico. BUAP — Hacer Ciencia en la BUAP, Otoño 2026.
