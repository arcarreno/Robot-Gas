import { useCallback, useEffect, useRef, useState } from 'react';
import { supabase } from '../lib/supabase';
import { LecturaDB, Punto, RobotData } from '../types/robot';

const MAX_TRAYECTORIA = 1500;
const MAX_PUNTOS_GAS = 200;
const MS_SIN_DATOS = 15000;

/**
 * Unico enlace de la app: Supabase Realtime (WiFi).
 * El ESP32 publica en lecturas_robot y la app recibe cada INSERT.
 */
export function useRobotData() {
  const [data, setData] = useState<RobotData | null>(null);
  const [trajectory, setTrajectory] = useState<Punto[]>([]);
  const [gasPoints, setGasPoints] = useState<Punto[]>([]);
  const [conectado, setConectado] = useState(false);
  const [sinDatos, setSinDatos] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [resetContador, setResetContador] = useState(0);
  const [intento, setIntento] = useState(0);

  const ultimoDatoRef = useRef(0);
  const gasActivoRef = useRef(false);

  const aplicar = useCallback((robot: RobotData) => {
    ultimoDatoRef.current = Date.now();
    setData(robot);

    const p = { x: robot.pose.x, y: robot.pose.y };
    setTrajectory(prev =>
      prev.length >= MAX_TRAYECTORIA
        ? [...prev.slice(prev.length - MAX_TRAYECTORIA + 1), p]
        : [...prev, p],
    );
  }, []);

  const aplicarDB = useCallback(
    (l: LecturaDB) => {
      const alerta = !!l.gas_alerta;

      // Un punto rojo por evento de gas (flanco de subida), no uno por lectura.
      if (alerta && !gasActivoRef.current) {
        const p = { x: l.x_cm, y: l.y_cm };
        setGasPoints(prev =>
          prev.length >= MAX_PUNTOS_GAS
            ? [...prev.slice(prev.length - MAX_PUNTOS_GAS + 1), p]
            : [...prev, p],
        );
      }
      gasActivoRef.current = alerta;

      aplicar({
        pose: { x: l.x_cm, y: l.y_cm, theta: l.theta_rad },
        sensors: {
          front: l.front_cm,
          left: l.left_cm,
          right: l.right_cm,
          gas: l.gas_valor,
          gasAlerta: alerta,
          gasPpm: l.gas_ppm ?? -1,
        },
        timestamp: new Date(l.timestamp).getTime(),
      });
    },
    [aplicar],
  );

  // Aviso de "sin telemetria" aunque el canal siga conectado.
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

  useEffect(() => {
    setError(null);
    setConectado(false);

    // Carga inmediata de la ultima lectura (no esperar al siguiente INSERT).
    supabase
      .from('lecturas_robot')
      .select('*')
      .order('timestamp', { ascending: false })
      .limit(1)
      .maybeSingle()
      .then(({ data: ultima, error: err }) => {
        if (ultima) aplicarDB(ultima as LecturaDB);
        if (err) setError(err.message);
      });

    const channel = supabase
      .channel(`rmadg1-${intento}`)
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
          setError('No se pudo conectar con Supabase Realtime.');
        }
      });

    return () => {
      supabase.removeChannel(channel);
      setConectado(false);
    };
  }, [intento, aplicarDB]);

  const reconectar = useCallback(() => setIntento(n => n + 1), []);

  const reiniciarMapa = useCallback(() => {
    setTrajectory([]);
    setGasPoints([]);
    setResetContador(n => n + 1);
  }, []);

  return {
    data,
    trajectory,
    gasPoints,
    conectado,
    sinDatos,
    error,
    resetContador,
    reconectar,
    reiniciarMapa,
  };
}
