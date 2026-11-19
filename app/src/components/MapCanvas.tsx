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

  // Encuadre automatico hasta que el usuario toca el mapa.
  const [autoVista, setAutoVista] = useState(true);
  const escalaRef = useRef(1);
  const centroRef = useRef({ x: 0, y: 0 });

  const soltarAuto = () => setAutoVista(false);

  useEffect(() => {
    setAutoVista(true);
    scale.value = 1;
    savedScale.value = 1;
    tx.value = 0;
    savedTx.value = 0;
    ty.value = 0;
    savedTy.value = 0;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [resetKey]);

  const pinch = Gesture.Pinch()
    .onBegin(() => {
      runOnJS(soltarAuto)();
    })
    .onUpdate(e => {
      scale.value = Math.min(Math.max(savedScale.value * e.scale, 0.5), 8);
    })
    .onEnd(() => {
      savedScale.value = scale.value;
    });

  const pan = Gesture.Pan()
    .onBegin(() => {
      runOnJS(soltarAuto)();
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
    ...gasPoints,
    ...(robot ? [{ x: robot.x, y: robot.y }] : []),
  ];

  if (pts.length === 0) {
    return (
      <View style={[styles.empty, { width, height }]}>
        <Text style={styles.emptyIcon}>🗺️</Text>
        <Text style={styles.emptyTitle}>Mapa en espera</Text>
        <Text style={styles.emptyText}>
          Aparecerá la ruta del robot en cuanto llegue la primera lectura.
        </Text>
      </View>
    );
  }

  const xs = pts.map(p => p.x);
  const ys = pts.map(p => p.y);
  const minX = Math.min(...xs);
  const maxX = Math.max(...xs);
  const minY = Math.min(...ys);
  const maxY = Math.max(...ys);

  const pad = 26;
  // Ventana minima de 40 cm: con el robot quieto no se genera un zoom absurdo.
  const sx = Math.max(maxX - minX, 40);
  const sy = Math.max(maxY - minY, 40);
  const baseAuto = Math.min((width - pad * 2) / sx, (height - pad * 2) / sy);

  if (autoVista) {
    escalaRef.current = baseAuto;
    centroRef.current = { x: (minX + maxX) / 2, y: (minY + maxY) / 2 };
  }
  const baseScale = escalaRef.current;
  const centroX = centroRef.current.x;
  const centroY = centroRef.current.y;
  const mapX = (x: number) => width / 2 + (x - centroX) * baseScale;
  const mapY = (y: number) => height / 2 - (y - centroY) * baseScale;
  const arrow = 20;

  return (
    <View
      style={[styles.wrapper, { width, height }]}
      accessibilityLabel="Mapa de ruta del robot. Puntos rojos indican gas detectado."
    >
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
                  strokeDasharray="4 6"
                />
                <Line
                  x1={0}
                  y1={height * f}
                  x2={width}
                  y2={height * f}
                  stroke={colors.grid}
                  strokeWidth={1}
                  strokeDasharray="4 6"
                />
              </G>
            ))}

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
                <Circle cx={mapX(p.x)} cy={mapY(p.y)} r={9} fill={colors.danger} opacity={0.18} />
                <Circle cx={mapX(p.x)} cy={mapY(p.y)} r={4} fill={colors.danger} />
              </G>
            ))}

            {robot && (
              <G>
                <Circle
                  cx={mapX(robot.x)}
                  cy={mapY(robot.y)}
                  r={11}
                  fill={colors.primary}
                  opacity={0.18}
                />
                <Circle cx={mapX(robot.x)} cy={mapY(robot.y)} r={6} fill={colors.primary} />
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
    backgroundColor: colors.surface,
    borderRadius: 16,
    borderWidth: 1,
    borderColor: colors.border,
  },
  empty: {
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: colors.surface,
    borderRadius: 16,
    borderWidth: 1,
    borderColor: colors.border,
    padding: 24,
  },
  emptyIcon: { fontSize: 34, marginBottom: 8 },
  emptyTitle: { fontSize: 16, fontWeight: '700', color: colors.text },
  emptyText: {
    fontSize: 13,
    color: colors.textMuted,
    textAlign: 'center',
    marginTop: 6,
    lineHeight: 19,
  },
  leyenda: {
    position: 'absolute',
    left: 10,
    bottom: 10,
    flexDirection: 'row',
    gap: 12,
    backgroundColor: 'rgba(255,255,255,0.92)',
    paddingHorizontal: 10,
    paddingVertical: 6,
    borderRadius: 999,
  },
  leyendaItem: { flexDirection: 'row', alignItems: 'center', gap: 4 },
  muestra: { width: 8, height: 8, borderRadius: 4 },
  leyendaTexto: { fontSize: 11, color: colors.textSecondary, fontWeight: '600' },
});
