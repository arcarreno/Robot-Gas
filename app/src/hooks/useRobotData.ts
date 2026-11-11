import { useCallback, useEffect, useRef, useState } from 'react';
import { PermissionsAndroid, Platform } from 'react-native';
import type { BluetoothDevice } from 'react-native-bluetooth-classic';
import { supabase } from '../lib/supabase';
import { LecturaDB, Punto, RobotData } from '../types/robot';
import {
  conectarDispositivo,
  dispositivosEmparejados,
  liberarDispositivo,
  suscribirDesconexion,
} from '../services/bluetooth';

export type ModoConexion = 'wifi' | 'bluetooth';

const MAX_TRAYECTORIA = 1500;
const MAX_OBSTACULOS = 900;
const MS_SIN_DATOS = 15000;

/**
 * Android 12+ exige BLUETOOTH_CONNECT en el momento exacto de abrir el socket.
 * Si el permiso no está concedido, la librería nativa lanza SecurityException
 * en un hilo y el proceso muere sin pasar por el catch de JavaScript.
 * Por eso se verifica aquí y se corta con un mensaje claro.
 */
async function asegurarPermisoConexion(): Promise<void> {
  if (Platform.OS !== 'android') return;

  if (Number(Platform.Version) >= 31) {
    const connect = await PermissionsAndroid.check(
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
    );
    if (connect) return;

    const result = await PermissionsAndroid.request(
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
      {
        title: 'Permiso de Bluetooth',
        message:
          'RobotCarr necesita "Dispositivos cercanos" para conectarse al robot por Bluetooth clásico.',
        buttonPositive: 'Permitir',
        buttonNegative: 'Cancelar',
      },
    );
    if (result !== PermissionsAndroid.RESULTS.GRANTED) {
      throw new Error(
        'Falta el permiso "Dispositivos cercanos". Actívalo en Ajustes > Apps > RobotCarr > Permisos.',
      );
    }
  } else {
    const fine = await PermissionsAndroid.check(
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
    );
    if (!fine) {
      const result = await PermissionsAndroid.request(
        PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
      );
      if (result !== PermissionsAndroid.RESULTS.GRANTED) {
        throw new Error(
          'Falta el permiso de ubicación, necesario para Bluetooth en Android 11 o menor.',
        );
      }
    }
  }
}

export function useRobotData(modo: ModoConexion) {
  const [data, setData] = useState<RobotData | null>(null);
  const [trajectory, setTrajectory] = useState<Punto[]>([]);
  const [obstacles, setObstacles] = useState<Punto[]>([]);
  const [conectado, setConectado] = useState(false);
  const [sinDatos, setSinDatos] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [log, setLog] = useState<string[]>([]);
  const [resetContador, setResetContador] = useState(0);

  const btRef = useRef<BluetoothDevice | null>(null);
  const btSubRef = useRef<{ remove: () => void } | null>(null);
  const ultimoDatoRef = useRef(0);

  const aplicar = useCallback((robot: RobotData) => {
    ultimoDatoRef.current = Date.now();
    setData(robot);

    const p = { x: robot.pose.x, y: robot.pose.y };
    setTrajectory(prev =>
      prev.length >= MAX_TRAYECTORIA
        ? [...prev.slice(prev.length - MAX_TRAYECTORIA + 1), p]
        : [...prev, p],
    );

    const { x, y, theta } = robot.pose;
    const puntos = [
      { d: robot.sensors.front, a: 0 },
      { d: robot.sensors.left, a: -Math.PI / 2 },
      { d: robot.sensors.right, a: Math.PI / 2 },
    ].map(({ d, a }) => ({
      x: x + d * Math.cos(theta + a),
      y: y + d * Math.sin(theta + a),
    }));

    setObstacles(prev =>
      prev.length >= MAX_OBSTACULOS
        ? [...prev.slice(prev.length - MAX_OBSTACULOS + 3), ...puntos]
        : [...prev, ...puntos],
    );
  }, []);

  const aplicarDB = useCallback(
    (l: LecturaDB) => {
      aplicar({
        pose: { x: l.x_cm, y: l.y_cm, theta: l.theta_rad },
        sensors: {
          front: l.front_cm,
          left: l.left_cm,
          right: l.right_cm,
          gas: l.gas_valor,
          gasAlerta: l.gas_alerta,
          gasPpm: l.gas_ppm ?? -1,
        },
        timestamp: new Date(l.timestamp).getTime(),
      });
    },
    [aplicar],
  );

  // Aviso: hay enlace pero el robot no está enviando nada
  useEffect(() => {
    if (!conectado) {
      setSinDatos(true);
      return;
    }
    const t = setInterval(() => {
      setSinDatos(Date.now() - ultimoDatoRef.current > MS_SIN_DATOS);
    }, 1000);
    return () => clearInterval(t);
  }, [conectado]);

  // ---------- Modo WiFi: Supabase Realtime ----------
  useEffect(() => {
    if (modo !== 'wifi') {
      return;
    }

    setError(null);

    // Carga inmediata de la última lectura: no esperar al siguiente INSERT.
    supabase
      .from('lecturas_robot')
      .select('*')
      .order('timestamp', { ascending: false })
      .limit(1)
      .maybeSingle()
      .then(({ data: ultima }) => {
        if (ultima) aplicarDB(ultima as LecturaDB);
      });

    const channel = supabase
      .channel('lecturas-robot')
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'lecturas_robot' },
        payload => aplicarDB(payload.new as LecturaDB),
      )
      .subscribe(status => {
        if (status === 'SUBSCRIBED') {
          setConectado(true);
          setError(null);
        }
        if (status === 'CHANNEL_ERROR' || status === 'TIMED_OUT') {
          setConectado(false);
          setError('No se pudo conectar a Supabase Realtime');
        }
      });

    return () => {
      supabase.removeChannel(channel);
      setConectado(false);
    };
  }, [modo, aplicarDB]);

  // ---------- Modo Bluetooth: SPP directo ----------
  const conectarBluetooth = useCallback(
    async (address: string) => {
      setError(null);
      setLog([]);
      ultimoDatoRef.current = 0;
      try {
        await asegurarPermisoConexion();
        const devices = await dispositivosEmparejados();
        const device = devices.find(d => d.address === address);
        if (!device) {
          throw new Error('Dispositivo no encontrado. Empareja el robot primero.');
        }

        const conn = await conectarDispositivo(device, line => {
          let esTelemetria = false;
          try {
            const j = JSON.parse(line);
            if (j.pose && j.sensors) {
              esTelemetria = true;
              aplicar({
                pose: {
                  x: Number(j.pose.x) * 100, // firmware envía metros -> cm
                  y: Number(j.pose.y) * 100,
                  theta: Number(j.pose.theta),
                },
                sensors: {
                  front: Number(j.sensors.front ?? 0),
                  left: Number(j.sensors.left ?? 0),
                  right: Number(j.sensors.right ?? 0),
                  gas: Number(j.sensors.gas ?? 0),
                  gasAlerta: Boolean(j.sensors.gas_alerta),
                  gasPpm: Number(j.sensors.gas_ppm ?? -1),
                },
                timestamp: Date.now(),
              });
            }
          } catch {
            // no es JSON
          }
          if (!esTelemetria) {
            // ACK/errores/colisiones: se muestran en pantalla (sin adb)
            console.log('[BT RX]', line);
            setLog(prev => [...prev.slice(-9), line]);
          }
        });

        btSubRef.current = suscribirDesconexion(() => {
          setConectado(false);
          setLog(prev => [...prev.slice(-9), 'BT: desconectado']);
        });
        btRef.current = conn;
        setConectado(true);
      } catch (e: any) {
        setError(e?.message ?? 'Error al conectar por Bluetooth');
        setConectado(false);
        setLog(prev => [...prev.slice(-9), `ERROR: ${e?.message ?? e}`]);
      }
    },
    [aplicar],
  );

  const desconectarBluetooth = useCallback(async () => {
    btSubRef.current?.remove();
    btSubRef.current = null;
    liberarDispositivo(btRef.current);
    try {
      await btRef.current?.disconnect();
    } catch {
      // ignorar
    }
    btRef.current = null;
    setConectado(false);
  }, []);

  const reiniciarMapa = useCallback(() => {
    setTrajectory([]);
    setObstacles([]);
    setResetContador(n => n + 1);   // el mapa también recupera zoom y paneo
  }, []);

  return {
    data,
    trajectory,
    obstacles,
    conectado,
    sinDatos,
    error,
    log,
    resetContador,
    btRef,
    conectarBluetooth,
    desconectarBluetooth,
    reiniciarMapa,
  };
}
