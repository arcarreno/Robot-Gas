import RNBluetoothClassic, {
  BluetoothDevice,
} from 'react-native-bluetooth-classic';

/**
 * Limpieza asociada a cada dispositivo conectado (listener + polling de respaldo).
 * Se guarda aparte para no cambiar la firma pública de conectarDispositivo().
 */
const limpiezas = new WeakMap<BluetoothDevice, () => void>();

export async function asegurarBluetooth(): Promise<void> {
  const enabled = await RNBluetoothClassic.isBluetoothEnabled();
  if (!enabled) {
    await RNBluetoothClassic.requestBluetoothEnabled();
  }
}

export async function dispositivosEmparejados(): Promise<BluetoothDevice[]> {
  await asegurarBluetooth();
  return RNBluetoothClassic.getBondedDevices();
}

/**
 * Abre el canal SPP y entrega las líneas COMPLETAS que llegan del robot.
 *
 * Dos detalles importantes:
 *  - El listener se registra ANTES de conectar: el ESP32 manda "Robot listo"
 *    en el mismo instante en que acepta la conexión.
 *  - Se acumula en un buffer y se corta por '\n': un evento nativo puede traer
 *    media línea y no se puede parsear el JSON a medias.
 */
export async function conectarDispositivo(
  device: BluetoothDevice,
  onLine: (line: string) => void,
): Promise<BluetoothDevice> {
  let buffer = '';
  let ultimoDato = Date.now();
  let vivo = true;

  const alimentar = (chunk: string) => {
    if (!chunk) return;
    buffer += chunk;
    let idx = buffer.indexOf('\n');
    while (idx >= 0) {
      const linea = buffer.slice(0, idx).replace(/\r/g, '').trim();
      buffer = buffer.slice(idx + 1);
      if (linea) onLine(linea);
      idx = buffer.indexOf('\n');
    }
    if (buffer.length > 8192) buffer = ''; // protección ante basura
  };

  const sub = device.onDataReceived(event => {
    ultimoDato = Date.now();
    alimentar(event.data);
  });

  let connected = false;
  try {
    const conexion = device.connect({
      connectorType: 'rfcomm',
      connectionType: 'delimited',
      delimiter: '\n',
      secure: false,
      readSize: 2048,
    });
    const timeout = new Promise<never>((_, reject) => {
      setTimeout(
        () => reject(new Error('Tiempo de conexión agotado (12 s)')),
        12000,
      );
    });
    connected = await Promise.race([conexion, timeout]);
  } catch (e) {
    sub.remove();
    throw e;
  }

  if (!connected) {
    sub.remove();
    throw new Error(`No se pudo conectar a ${device.name ?? device.address}`);
  }

  // Respaldo: en algunos Android el evento DEVICE_READ no dispara. Si pasan
  // 2.5 s sin eventos, se lee el buffer nativo directamente.
  const timer = setInterval(async () => {
    if (!vivo) return;
    if (Date.now() - ultimoDato < 2500) return;
    try {
      const hay = await device.available();
      if (hay && hay > 0) {
        const txt = await device.read();
        if (txt) {
          ultimoDato = Date.now();
          alimentar(String(txt));
        }
      }
    } catch {
      // dispositivo desconectado: lo detecta onDeviceDisconnected
    }
  }, 400);

  const liberar = () => {
    vivo = false;
    clearInterval(timer);
    sub.remove();
    limpiezas.delete(device);
  };
  limpiezas.set(device, liberar);

  return device;
}

/** Detiene el polling y el listener asociados a un dispositivo. */
export function liberarDispositivo(device: BluetoothDevice | null): void {
  if (!device) return;
  const fn = limpiezas.get(device);
  if (fn) fn();
}

export function suscribirDesconexion(listener: () => void) {
  return RNBluetoothClassic.onDeviceDisconnected(() => listener());
}

export async function enviarComando(
  conn: BluetoothDevice | null,
  cmd: string,
): Promise<void> {
  if (!conn) {
    throw new Error(
      'Sin conexión Bluetooth activa. Elige RobotBT y espera "Conectado".',
    );
  }
  const payload = `${cmd}\n`;
  console.log('[BT TX]', JSON.stringify(payload));
  const ok = await conn.write(payload);
  if (ok === false) {
    throw new Error('El envío por Bluetooth fue rechazado');
  }
}
