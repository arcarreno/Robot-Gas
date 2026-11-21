import { useCallback, useEffect, useRef, useState } from 'react';
import { supabase } from '../lib/supabase';
import { LecturaDB, Punto, RobotData } from '../types/robot';

const MAX_TRAYECTORIA = 2000;
const MAX_PUNTOS_GAS = 300;
const MS_SIN_DATOS = 15000;
const LOTE = 30;

/**
 * Unico enlace de la app: Supabase. Se consulta un LOTE de lecturas (no solo
 * la ultima) para no perder eventos de gas que duran una sola lectura.
 */
export function useRobotData() {
  const [data, setData] = useState<RobotData | null>(null);
  const [trajectory, setTrajectory] = useState<Punto[]>([]);
  const [gasPoints, setGasPoints] = useState<Punto[]>([]);
  const [conectado, setConectado] = useState(false);
  const [sinDatos, setSinDatos] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [resetContador, setResetContador] = useState(0);

  const ultimoIdRef = useRef(0);
  const ultimoDatoRef = useRef(0);
  const gasActivoRef = useRef(false);

  const aplicar = useCallback((robot: RobotData) => {
    ultimoDatoRef.current = Date.now();
    setData(robot);

    const p = { x: robot.pose.x, y: robot.pose.y };
    setTrajectory(prev => {
      const last = prev[prev.length - 1];
      // Los giros en el sitio generan many puntos iguales: se ignoran.
      if (last && Math.abs(last.x - p.x) < 0.5 && Math.abs(last.y - p.y) < 0.5) {
        return prev;
      }
      return prev.length >= MAX_TRAYECTORIA
        ? [...prev.slice(prev.length - MAX_TRAYECTORIA + 1), p]
        : [...prev, p];
    });
  }, []);

  const aplicarDB = useCallback(
    (l: LecturaDB) => {
      const alerta = !!l.gas_alerta;

      // Un punto rojo por flanco de subida, no uno por lectura.
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

  // Procesa todas las filas nuevas, en orden ascendente: asi no se pierde
  // un gas que duro una sola lectura entre dos sondeos.
  const procesarLote = useCallback(
    (filas: LecturaDB[]) => {
      const nuevas = [...filas]
        .filter(f => f.id > ultimoIdRef.current)
        .sort((a, b) => a.id - b.id);
      for (const fila of nuevas) {
        ultimoIdRef.current = fila.id;
        aplicarDB(fila);
      }
      setConectado(true);
      setError(null);
    },
    [aplicarDB],
  );

  useEffect(() => {
    async function cargar() {
      const { data: filas, error: err } = await supabase
        .from('lecturas_robot')
        .select('*')
        .order('id', { ascending: false })
        .limit(LOTE);
      if (err) {
        setError(err.message);
        return;
      }
      if (filas?.length) procesarLote(filas as LecturaDB[]);
    }

    cargar();
    const t = setInterval(cargar, 1000);
    return () => clearInterval(t);
  }, [procesarLote]);

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
    reiniciarMapa,
  };
}
