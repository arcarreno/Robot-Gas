import React, { useEffect, useRef, useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import Svg, { Circle, G, Line, Polyline } from 'react-native-svg';
import { Gesture, GestureDetector } from 'react-native-gesture-handler';
import Animated, {
  runOnJS,
  useAnimatedStyle,
  useSharedValue,
  withTiming,
} from 'react-native-reanimated';
import { Punto } from '../types/robot';
import { colors } from '../theme';

type Props = {
  trajectory: Punto[];
  /** Solo puntos donde hubo alarma de gas. Los obstaculos no se pintan. */
  gasPoints: Punto[];
  robot: { x: number; y: number; theta: number } | null;
  width: number;
  height: number;
  resetKey?: number;
};

const MUNDO_CM = 600;
const PAD = 28;
const SCALE_MIN = 0.02; // px por cm: nunca se aleja mas de esto

type Bounds = { minX: number; maxX: number; minY: number; maxY: number };

function calcularBounds(pts: Punto[]): Bounds | null {
  if (pts.length === 0) return null;
  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  for (const p of pts) {
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
  }
  return { minX, maxX, minY, maxY };
}

export function MapCanvas({
  trajectory,
  gasPoints,
  robot,
  width,
  height,
  resetKey = 0,
}: Props) {
  const scale = useSharedValue(1);
  const savedScale = useSharedValue(1);
  const tx = useSharedValue(0);
  const savedTx = useSharedValue(0);
  const ty = useSharedValue(0);
  const savedTy = useSharedValue(0);

  const [autoVista, setAutoVista] = useState(true);
  const centroRef = useRef({ x: 0, y: 0 });
  const autoScaleRef = useRef(width / MUNDO_CM);

  const recentrar = () => {
    setAutoVista(true);
    scale.value = withTiming(1);
    tx.value = withTiming(0);
    ty.value = withTiming(0);
    savedScale.value = 1;
    savedTx.value = 0;
    savedTy.value = 0;
  };

  useEffect(() => {
    autoScaleRef.current = width / MUNDO_CM;
    centroRef.current = robot ? { x: robot.x, y: robot.y } : { x: 0, y: 0 };
    recentrar();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [resetKey]);

  const pinch = Gesture.Pinch()
    .onBegin(() => {
      runOnJS(setAutoVista)(false);
    })
    .onUpdate(e => {
      scale.value = Math.min(Math.max(savedScale.value * e.scale, 0.4), 6);
    })
    .onEnd(() => {
      savedScale.value = scale.value;
    });

  const pan = Gesture.Pan()
    .onBegin(() => {
      runOnJS(setAutoVista)(false);
    })
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
      runOnJS(recentrar)();
    });

  const gesture = Gesture.Simultaneous(pinch, pan, doubleTap);

  const animatedStyle = useAnimatedStyle(() => ({
    transform: [
      { translateX: tx.value },
      { translateY: ty.value },
      { scale: scale.value },
    ],
  }));

  const todosPts: Punto[] = [
    ...trajectory,
    ...gasPoints,
    ...(robot ? [{ x: robot.x, y: robot.y }] : []),
  ];
  const bounds = calcularBounds(todosPts);

  if (autoVista && bounds) {
    const centroObjetivo = {
      x: (bounds.minX + bounds.maxX) / 2,
      y: (bounds.minY + bounds.maxY) / 2,
    };
    // Seguimiento suave. Al calcular la escala se incluye el desfase del
    // centro para que nada quede fuera aunque el robot se mueva rapido.
    centroRef.current.x += (centroObjetivo.x - centroRef.current.x) * 0.4;
    centroRef.current.y += (centroObjetivo.y - centroRef.current.y) * 0.4;
    const desfaseX = Math.abs(centroRef.current.x - centroObjetivo.x);
    const desfaseY = Math.abs(centroRef.current.y - centroObjetivo.y);

    const spanX = Math.max(bounds.maxX - bounds.minX, 40) + 2 * desfaseX;
    const spanY = Math.max(bounds.maxY - bounds.minY, 40) + 2 * desfaseY;
    const deseada = Math.min(
      (width - PAD * 2) / spanX,
      (height - PAD * 2) / spanY,
    );
    // NUNCA se acerca: asi la ruta trazada no se comprime ni "desaparece"
    // cuando el robot vuelve a una zona ya visitada.
    if (deseada > SCALE_MIN && deseada < autoScaleRef.current) {
      autoScaleRef.current = deseada;
    }
  }

  const baseScale = autoScaleRef.current;
  const centroX = centroRef.current.x;
  const centroY = centroRef.current.y;
  const mapX = (x: number) => width / 2 + (x - centroX) * baseScale;
  const mapY = (y: number) => height / 2 - (y - centroY) * baseScale;
  const arrow = 18;

  const sinDatos = todosPts.length === 0;

  return (
    <View style={[styles.wrapper, { width, height }]}>
      <GestureDetector gesture={gesture}>
        <Animated.View style={[{ width, height }, animatedStyle]}>
          <Svg width={width} height={height}>
            {[0.25, 0.5, 0.75].map(f => (
              <G key={`grid-${f}`}>
                <Line
                  x1={width * f}
                  y1={0}
                  x2={width * f}
                  y2={height}
                  stroke={colors.grid}
                  strokeWidth={1}
                  strokeDasharray="3 7"
                />
                <Line
                  x1={0}
                  y1={height * f}
                  x2={width}
                  y2={height * f}
                  stroke={colors.grid}
                  strokeWidth={1}
                  strokeDasharray="3 7"
                />
              </G>
            ))}

            <G>
              {[0, 1, 2, 3].map(i => {
                const gx = (i - 1.5) * 100;
                const gy = (i - 1.5) * 100;
                return (
                  <G key={`m-${i}`}>
                    <Line
                      x1={mapX(gx)}
                      y1={0}
                      x2={mapX(gx)}
                      y2={height}
                      stroke={colors.grid}
                      strokeWidth={1}
                    />
                    <Line
                      x1={0}
                      y1={mapY(gy)}
                      x2={width}
                      y2={mapY(gy)}
                      stroke={colors.grid}
                      strokeWidth={1}
                    />
                  </G>
                );
              })}
            </G>

            {trajectory.length > 1 && (
              <Polyline
                points={trajectory.map(p => `${mapX(p.x)},${mapY(p.y)}`).join(' ')}
                fill="none"
                stroke={colors.route}
                strokeWidth={3}
                strokeLinecap="round"
                strokeLinejoin="round"
              />
            )}

            {gasPoints.map((p, i) => (
              <G key={`gas-${i}`}>
                <Circle cx={mapX(p.x)} cy={mapY(p.y)} r={10} fill={colors.danger} opacity={0.2} />
                <Circle cx={mapX(p.x)} cy={mapY(p.y)} r={4.5} fill={colors.danger} />
              </G>
            ))}

            {robot && (
              <G>
                <Circle cx={mapX(robot.x)} cy={mapY(robot.y)} r={12} fill={colors.primary} opacity={0.18} />
                <Circle cx={mapX(robot.x)} cy={mapY(robot.y)} r={6.5} fill={colors.primary} />
                <Line
                  x1={mapX(robot.x)}
                  y1={mapY(robot.y)}
                  x2={mapX(robot.x) + arrow * Math.cos(robot.theta)}
                  y2={mapY(robot.y) - arrow * Math.sin(robot.theta)}
                  stroke={colors.primaryDark}
                  strokeWidth={3}
                  strokeLinecap="round"
                />
              </G>
            )}
          </Svg>
        </Animated.View>
      </GestureDetector>

      {sinDatos && (
        <View style={styles.empty} pointerEvents="none">
          <Text style={styles.emptyText}>Esperando la ruta del robot…</Text>
        </View>
      )}

      <View style={styles.leyenda} pointerEvents="none">
        <View style={styles.leyendaItem}>
          <View style={[styles.muestra, { backgroundColor: colors.primary }]} />
          <Text style={styles.leyendaTexto}>Robot</Text>
        </View>
        <View style={styles.leyendaItem}>
          <View style={[styles.muestra, { backgroundColor: colors.route }]} />
          <Text style={styles.leyendaTexto}>Ruta</Text>
        </View>
        <View style={styles.leyendaItem}>
          <View style={[styles.muestra, { backgroundColor: colors.danger }]} />
          <Text style={styles.leyendaTexto}>Gas</Text>
        </View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  wrapper: {
    overflow: 'hidden',
    backgroundColor: colors.background,
  },
  empty: {
    position: 'absolute',
    top: 16,
    left: 0,
    right: 0,
    alignItems: 'center',
  },
  emptyText: { color: colors.textMuted, fontSize: 13 },
  leyenda: {
    position: 'absolute',
    left: 0,
    bottom: 8,
    flexDirection: 'row',
    gap: 14,
  },
  leyendaItem: { flexDirection: 'row', alignItems: 'center', gap: 5 },
  muestra: { width: 8, height: 8, borderRadius: 4 },
  leyendaTexto: { fontSize: 11, color: colors.textMuted, fontWeight: '600' },
});
