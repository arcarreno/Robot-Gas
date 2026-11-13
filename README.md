# Robot-Gas

**Etapa de comunicacion WiFi para un vehiculo terrestre autonomo**

Robot autonomo con ESP32 que detecta gas (MQ-2), se mueve solo evitando
obstaculos y reporta su estado en tiempo real a una app Android por WiFi
(a traves de Supabase).

- **Estudiante:** Angel Armando Carreno Gonzalez
- **Investigadora:** Alba Maribel Sanchez Galvez
- **Institucion:** BUAP - Facultad de Ciencias Fisico-Matematicas

---

## Que hace

| Funcion | Detalle |
|---|---|
| Navegacion | Avance recto con correccion de rumbo por giroscopio (MPU6050) |
| Evasion | Al detectar obstaculo se detiene y hace un **barrido de 360 grados** (8 pasos de 45), midiendo el frente en cada parada, y sale por el rumbo mas despejado |
| Deteccion de gas | MQ-2 leido desde el arranque: **AO** (GPIO36) y **DO** (GPIO23). Si hay gas el robot se detiene y la app parpadea |
| Comunicacion | Telemetria a Supabase por HTTPS (TLS) y comandos desde la app |
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
| Bateria | 2 x 18650 en serie (7.4 V) |

### Conexiones

```
MQ-2    VCC -> 5V      GND -> GND
        DO   -> GPIO23  (colector abierto: necesita pull-up)
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
  src/screens/            Dashboard y conexion
  src/components/         Medidor de gas y mapa
  src/hooks/              Datos en vivo (WiFi/Supabase + Bluetooth)
  android/                Proyecto Android nativo
docs/                     Esquema SQL
```

## Puesta en marcha

### 1. Firmware

1. Copia la plantilla de secretos:
   ```bash
   cp firmware/RMA_3S_Mapa/secrets.example.h firmware/RMA_3S_Mapa/secrets.h
   ```
   y rellena WiFi y Supabase. `secrets.h` esta en `.gitignore`, nunca se sube.
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

Se envian por USB o por la tabla `comandos_robot` de Supabase.

| Comando | Efecto |
|---|---|
| `ini` / `start` | Arranca. La primera vez calibra solo la polaridad de los motores (~2 s, robot en el piso) |
| `alto` / `stop` | Detiene el robot |
| `ping` | Responde `ACK:PONG` |
| `cal` | Vuelve a calibrar los motores (si se invirtio una rueda) |
| `gasoff` | Desactiva el corte por gas (la app sigue mostrando la alerta) |
| `gason` | Reactiva el corte por gas |
| `mq2` | Estado del sensor: AO en voltios, ppm estimadas, DO y si el DO es usable |

## Notas sobre el sensor de gas

El firmware **no calibra** el MQ-2: lo lee desde el primer segundo y manda
`gas_ppm` y `gas_do` en cada lectura.

- En el modulo instalado el **DO va en LOW con aire limpio** y pasa a **HIGH
  con gas**. La alarma se enciende cuando el DO se mantiene **500 ms en HIGH**
  y se apaga tras **1 s en LOW** (antirrebote).
- El **DO es de colector abierto**: necesita pull-up. El firmware lo activa.
- El **AO puede quedar en 0 V** si el modulo es de 3 pines o el AO no esta
  conectado; en ese caso el DO es la unica alarma y `gas_ppm` queda en 0.
- El DO se habilita la primera vez que se ve en LOW, para no confundir un pin
  al aire con gas. **Arranca el robot en aire limpio** una vez; despues la
  alarma queda activa.
- `gas_ppm` es una **estimacion lineal** para la barra de la app, no una medida
  de laboratorio (con el AO desconectado se queda en 0).
- Detecta **GLP, propano, metano, humo y alcohol** - no CO2. El GLP pesa mas
  que el aire: pon el sensor **bajo** y cerca de la fuente.
- Para probar: **encendedor con gas, sin llama**, pegado al sensor.

## Configuracion del ESP32

El firmware es solo WiFi (sin Bluetooth): asi libera ~95 kB de RAM para el TLS.
Las redes WiFi se declaran en `secrets.h`, en orden de preferencia.

## Licencia

Proyecto academico. BUAP - Hacer Ciencia en la BUAP, Otono 2026.