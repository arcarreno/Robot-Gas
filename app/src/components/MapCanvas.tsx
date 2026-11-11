import React, { useEffect, useRef, useState } from 'react';
import { View, StyleSheet, Text } from 'react-native';
import Svg, { Polyline, Circle, Line, G } from 'react-native-svg';
import { Gesture, GestureDetector } from 'react-native-gesture-handler';
import Animated, {
  runOnJS,
  useAnimatedStyle,
  useSharedValue,
  withTiming,
} from 'react-native-reanimated';
import { Punto } from '../types/robot';

type Props = {
  trajectory: Punto[];
  obstacles: Punto[];
  robot: { x: number; y: number; theta: number } | null;
  width: number;
  height: number;
  resetKey?: number;
};

export function MapCanvas({ trajectory, obstacles, robot, width, height, resetKey = 0 }: Props) {
  const scale = useSharedValue(1);
  const savedScale = useSharedValue(1);
  const tx = useSharedValue(0);
  const savedTx = useSharedValue(0);
  const ty = useSharedValue(0);
  const savedTy = useSharedValue(0);

  // Mientras no se toque el mapa, el encuadre es automático (el robot siempre
  // visible). En cuanto el usuario hace zoom o paneo, se congela el encuadre
  // para que sus gestos no sean sobrescritos por la telemetría siguiente.
  const [autoVista, setAutoVista] = useState(true);
  const escalaRef = useRef(1);
  const centroRef = useRef({ x: 0, y: 0 });

  const soltarAuto = () => setAutoVista(false);

  useEffect(() => {
    setAutoVista(true);
    scale.value = 1; savedScale.value = 1;
    tx.value = 0; savedTx.value = 0;
    ty.value = 0; savedTy.value = 0;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [resetKey]);

  const pinch = Gesture.Pinch()
    .onBegin(() => { runOnJS(soltarAuto)(); })
    .onUpdate(e => {
      const next = savedScale.value * e.scale;
      scale.value = Math.min(Math.max(next, 0.5), 8);
    })
    .onEnd(() => {
      savedScale.value = scale.value;
    });

  const pan = Gesture.Pan()
    .onBegin(() => { runOnJS(soltarAuto)(); })
    .onUpdate(e => {
      tx.value = savedTx.value + e.translationX;
      ty.value = savedTy.value + e.translationY;
    })
    .onEnd(() => {
      savedTx.value = tx.value;
      savedTy.value = ty.value;
    });

  const doubleTap = Gesture.Tap()
    .numberOfTaps(2)
    .onEnd(() => {
      scale.value = withTiming(1);
      tx.value = withTiming(0);
      ty.value = withTiming(0);
      savedScale.value = 1;
      savedTx.value = 0;
      savedTy.value = 0;
      runOnJS(soltarAuto)();
    });

  const gesture = Gesture.Simultaneous(pinch, pan, doubleTap);

  const animatedStyle = useAnimatedStyle(() => ({
    transform: [
      { translateX: tx.value },
      { translateY: ty.value },
      { scale: scale.value },
    ],
  }));

  const pts = [
    ...trajectory,
    ...obstacles,
    ...(robot ? [{ x: robot.x, y: robot.y }] : []),
  ];

  if (pts.length === 0) {
    return (
      <View style={[styles.empty, { width, height }]}>
        <Text style={styles.emptyText}>Esperando datos del robot…</Text>
      </View>
    );
  }

  const xs = pts.map(p => p.x);
  const ys = pts.map(p => p.y);
  const minX = Math.min(...xs);
  const maxX = Math.max(...xs);
  const minY = Math.min(...ys);
  const maxY = Math.max(...ys);

  const pad = 24;
  // Ventana mínima de 40 cm: con el robot quieto no se genera un zoom absurdo
  const sx = Math.max(maxX - minX, 40);
  const sy = Math.max(maxY - minY, 40);
  const baseAuto = Math.min((width - pad * 2) / sx, (height - pad * 2) / sy);
  const centroAutoX = (minX + maxX) / 2;
  const centroAutoY = (minY + maxY) / 2;

  // En modo automático el encuadre sigue al robot; si el usuario ya hizo
  // zoom o paneo, se mantiene el encuadre congelado.
  if (autoVista) {
    escalaRef.current = baseAuto;
    centroRef.current = { x: centroAutoX, y: centroAutoY };
  }
  const baseScale = escalaRef.current;
  const centroX = centroRef.current.x;
  const centroY = centroRef.current.y;
  const mapX = (x: number) => width / 2 + (x - centroX) * baseScale;
  const mapY = (y: number) => height / 2 - (y - centroY) * baseScale;
  const arrow = 18;

  return (
    <View style={[styles.wrapper, { width, height }]}>
      <GestureDetector gesture={gesture}>
        <Animated.View style={[{ width, height }, animatedStyle]}>
          <Svg width={width} height={height}>
            {obstacles.map((o, i) => (
              <Circle
                key={`o${i}`}
                cx={mapX(o.x)}
                cy={mapY(o.y)}
                r={2.5}
                fill="#EF4444"
              />
            ))}

            {trajectory.length > 1 && (
              <Polyline
                points={trajectory.map(p => `${mapX(p.x)},${mapY(p.y)}`).join(' ')}
                fill="none"
                stroke="#22C55E"
                strokeWidth={2}
              />
            )}

            {robot && (
              <G>
                <Circle cx={mapX(robot.x)} cy={mapY(robot.y)} r={5} fill="#2563EB" />
                <Line
                  x1={mapX(robot.x)}
                  y1={mapY(robot.y)}
                  x2={mapX(robot.x) + arrow * Math.cos(robot.theta)}
                  y2={mapY(robot.y) - arrow * Math.sin(robot.theta)}
                  stroke="#2563EB"
                  strokeWidth={2}
                />
              </G>
            )}
          </Svg>
        </Animated.View>
      </GestureDetector>
    </View>
  );
}

const styles = StyleSheet.create({
  wrapper: {
    overflow: 'hidden',
    backgroundColor: '#FFFFFF',
    borderRadius: 12,
  },
  empty: {
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: '#FFFFFF',
    borderRadius: 12,
  },
  emptyText: {
    color: '#64748B',
  },
});
