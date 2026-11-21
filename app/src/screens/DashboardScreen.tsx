import React, { useEffect, useRef, useState } from 'react';
import {
  Animated,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
  useWindowDimensions,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { MapCanvas } from '../components/MapCanvas';
import { GasGauge } from '../components/GasGauge';
import { Icon } from '../components/Icon';
import { useRobotData } from '../hooks/useRobotData';
import { supabase } from '../lib/supabase';
import { ROBOT_ID } from '../config';
import { colors, font, radius, space } from '../theme';

export function DashboardScreen() {
  const { width } = useWindowDimensions();
  const [aviso, setAviso] = useState<string | null>(null);
  const {
    data,
    trajectory,
    gasPoints,
    conectado,
    sinDatos,
    error,
    resetContador,
    reiniciarMapa,
  } = useRobotData();

  useEffect(() => {
    if (!aviso) return;
    const t = setTimeout(() => setAviso(null), 3500);
    return () => clearTimeout(t);
  }, [aviso]);

  async function enviar(cmd: string) {
    try {
      const { error: err } = await supabase
        .from('comandos_robot')
        .insert({ comando: cmd, robot_id: ROBOT_ID });
      if (err) throw err;
      setAviso(cmd === 'ini' ? 'Orden de inicio enviada' : 'Orden de parada enviada');
    } catch (e: any) {
      setAviso(e?.message ?? 'No se pudo enviar la orden');
    }
  }

  const gasAlerta = !!data?.sensors.gasAlerta;
  const anchoMapa = Math.min(width - space.lg * 2, 900);
  const numPuntos = trajectory.length;
  const numGas = gasPoints.length;

  const pulso = useRef(new Animated.Value(0)).current;
  useEffect(() => {
    if (!gasAlerta) {
      pulso.stopAnimation();
      pulso.setValue(0);
      return;
    }
    const ciclo = Animated.loop(
      Animated.sequence([
        Animated.timing(pulso, { toValue: 1, duration: 400, useNativeDriver: true }),
        Animated.timing(pulso, { toValue: 0, duration: 400, useNativeDriver: true }),
      ]),
    );
    ciclo.start();
    return () => ciclo.stop();
  }, [gasAlerta, pulso]);

  const ultimaHora = data ? horaLocal(data.timestamp) : '--:--:--';

  return (
    <View style={styles.root}>
      <SafeAreaView style={styles.safe} edges={['top', 'left', 'right']}>
        <ScrollView style={styles.scroll} contentContainerStyle={styles.content}>
          <View style={styles.header}>
            <View style={styles.marca}>
              <Text style={styles.marcaTexto}>R1</Text>
            </View>
            <View style={styles.headerTexto}>
              <Text style={styles.titulo}>RMADG1</Text>
              <Text style={styles.subtitulo}>Vehículo terrestre autónomo</Text>
            </View>
            <View style={styles.estado}>
              <View
                style={[
                  styles.punto,
                  { backgroundColor: conectado ? colors.success : colors.warning },
                ]}
              />
              <Text style={styles.estadoTexto}>
                {conectado ? 'En línea' : 'Sin enlace'}
              </Text>
            </View>
          </View>

          {!!error && <Text style={styles.error}>{error}</Text>}
          {conectado && sinDatos && (
            <Text style={styles.aviso}>Enlace listo. Esperando la primera lectura…</Text>
          )}
          {!!aviso && <Text style={styles.avisoInfo}>{aviso}</Text>}

          <GasGauge
            valor={data?.sensors.gas ?? 0}
            alerta={gasAlerta}
            ppm={data?.sensors.gasPpm}
          />

          <Seccion titulo="MAPA DE RUTA" meta={`${numPuntos} puntos · ${numGas} gas`}>
            <MapCanvas
              trajectory={trajectory}
              gasPoints={gasPoints}
              robot={data ? data.pose : null}
              width={anchoMapa}
              height={320}
              resetKey={resetContador}
            />
            <Text style={styles.pista}>
              Pellizca · arrastra · doble toque para centrar
            </Text>
          </Seccion>

          <Seccion titulo="DISTANCIAS" meta="ultrasónicos">
            <View style={styles.metricas}>
              <Metrica etiqueta="FRENTE" valor={data?.sensors.front ?? 0} />
              <Metrica etiqueta="IZQ" valor={data?.sensors.left ?? 0} />
              <Metrica etiqueta="DER" valor={data?.sensors.right ?? 0} />
            </View>
          </Seccion>

          <Seccion titulo="POSICIÓN" meta={ultimaHora}>
            <View style={styles.filas}>
              <Dato etiqueta="X" valor={data ? `${data.pose.x.toFixed(0)} cm` : '—'} />
              <Dato etiqueta="Y" valor={data ? `${data.pose.y.toFixed(0)} cm` : '—'} />
              <Dato
                etiqueta="RUMBO"
                valor={data ? `${(data.pose.theta * 180 / Math.PI).toFixed(0)}°` : '—'}
              />
            </View>
          </Seccion>

          <View style={styles.controles}>
            <Pressable
              style={[styles.boton, styles.botonIniciar]}
              onPress={() => enviar('ini')}
              accessibilityRole="button"
              accessibilityLabel="Iniciar el robot"
            >
              <View style={styles.botonContenido}>
                <Icon nombre="play" size={18} color="#FFFFFF" />
                <Text style={styles.botonTexto}>Iniciar</Text>
              </View>
            </Pressable>
            <Pressable
              style={[styles.boton, styles.botonDetener]}
              onPress={() => enviar('alto')}
              accessibilityRole="button"
              accessibilityLabel="Detener el robot"
            >
              <View style={styles.botonContenido}>
                <Icon nombre="stop" size={16} color="#FFFFFF" />
                <Text style={styles.botonTexto}>Detener</Text>
              </View>
            </Pressable>
          </View>
          <Pressable
            style={styles.botonSecundario}
            onPress={reiniciarMapa}
            accessibilityRole="button"
            accessibilityLabel="Limpiar el mapa"
          >
            <Text style={styles.botonSecundarioTexto}>Limpiar mapa</Text>
          </Pressable>

          <Text style={styles.pie}>RMADG1 · Supabase · {ultimaHora}</Text>
        </ScrollView>
      </SafeAreaView>

      {gasAlerta && (
        <>
          <Animated.View
            pointerEvents="none"
            style={[
              StyleSheet.absoluteFill,
              styles.brillo,
              {
                opacity: pulso.interpolate({
                  inputRange: [0, 1],
                  outputRange: [0.06, 0.38],
                }),
              },
            ]}
          />
          <View pointerEvents="none" style={styles.bandaAlerta}>
            <View style={styles.bandaContenido}>
            <Icon nombre="alerta" size={20} color="#FFFFFF" />
            <Text style={styles.bandaTexto}>GAS DETECTADO · robot detenido</Text>
          </View>
          </View>
        </>
      )}
    </View>
  );
}

function Seccion({
  titulo,
  meta,
  children,
}: {
  titulo: string;
  meta?: string;
  children: React.ReactNode;
}) {
  return (
    <View style={styles.seccion}>
      <View style={styles.seccionHeader}>
        <Text style={styles.seccionTitulo}>{titulo}</Text>
        {!!meta && <Text style={styles.seccionMeta}>{meta}</Text>}
      </View>
      {children}
    </View>
  );
}

function Metrica({ etiqueta, valor }: { etiqueta: string; valor: number }) {
  const activo = valor > 0;
  const pct = activo ? Math.min(100, (valor / 300) * 100) : 0;
  return (
    <View style={styles.metrica}>
      <Text style={styles.metricaEtiqueta}>{etiqueta}</Text>
      <Text style={styles.metricaValor}>{activo ? valor.toFixed(0) : '—'}</Text>
      <View style={styles.metricaPista}>
        <View style={[styles.metricaBarra, { width: `${pct}%` }]} />
      </View>
    </View>
  );
}

function Dato({ etiqueta, valor }: { etiqueta: string; valor: string }) {
  return (
    <View style={styles.dato}>
      <Text style={styles.datoEtiqueta}>{etiqueta}</Text>
      <Text style={styles.datoValor}>{valor}</Text>
    </View>
  );
}

function horaLocal(ms: number) {
  const d = new Date(ms);
  const dos = (n: number) => String(n).padStart(2, '0');
  return `${dos(d.getHours())}:${dos(d.getMinutes())}:${dos(d.getSeconds())}`;
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: colors.background },
  safe: { flex: 1 },
  scroll: { flex: 1 },
  content: { padding: space.lg, paddingBottom: space.xxl },
  brillo: { backgroundColor: colors.danger },
  bandaAlerta: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    backgroundColor: colors.danger,
    paddingVertical: 12,
    paddingHorizontal: space.lg,
    alignItems: 'center',
  },
  bandaContenido: { flexDirection: 'row', alignItems: 'center', gap: space.sm },
  bandaTexto: {
    color: '#FFFFFF',
    fontWeight: '800',
    fontSize: font.lg,
    letterSpacing: 0.4,
  },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    paddingBottom: space.md,
    borderBottomWidth: 1,
    borderBottomColor: colors.border,
  },
  headerTexto: { flex: 1 },
  marca: {
    width: 44,
    height: 44,
    borderRadius: 12,
    backgroundColor: colors.primary,
    alignItems: 'center',
    justifyContent: 'center',
  },
  marcaTexto: { color: '#FFFFFF', fontWeight: '800', fontSize: font.lg },
  titulo: { fontSize: font.xxl, fontWeight: '800', color: colors.text, lineHeight: 32 },
  subtitulo: { fontSize: font.sm, color: colors.textMuted, marginTop: 2 },
  estado: { flexDirection: 'row', alignItems: 'center', gap: 6 },
  punto: { width: 8, height: 8, borderRadius: 4 },
  estadoTexto: { fontSize: font.sm, color: colors.textSecondary, fontWeight: '700' },
  error: { color: colors.danger, fontSize: font.sm, marginTop: space.sm },
  aviso: { color: colors.warning, fontSize: font.sm, marginTop: space.sm },
  avisoInfo: { color: colors.primary, fontSize: font.sm, marginTop: space.sm },
  seccion: { marginTop: space.xl },
  seccionHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'baseline',
    paddingBottom: space.sm,
  },
  seccionTitulo: {
    fontSize: font.sm,
    fontWeight: '800',
    color: colors.textSecondary,
    letterSpacing: 1,
  },
  seccionMeta: { fontSize: font.xs, color: colors.textMuted },
  pista: { fontSize: font.xs, color: colors.textMuted, marginTop: space.sm },
  metricas: { flexDirection: 'row', gap: space.lg },
  metrica: { flex: 1 },
  metricaEtiqueta: { fontSize: font.xs, color: colors.textMuted, fontWeight: '700' },
  metricaValor: { fontSize: 30, fontWeight: '800', color: colors.text, marginTop: 2 },
  metricaPista: {
    height: 4,
    borderRadius: 2,
    backgroundColor: colors.border,
    marginTop: space.sm,
  },
  metricaBarra: { height: 4, borderRadius: 2, backgroundColor: colors.primary },
  filas: { flexDirection: 'row', gap: space.xl },
  dato: { flex: 1 },
  datoEtiqueta: { fontSize: font.xs, color: colors.textMuted, fontWeight: '700' },
  datoValor: { fontSize: font.xl, fontWeight: '800', color: colors.text, marginTop: 2 },
  controles: {
    flexDirection: 'row',
    gap: space.md,
    marginTop: space.xl,
  },
  boton: {
    flex: 1,
    minHeight: 54,
    borderRadius: radius.md,
    alignItems: 'center',
    justifyContent: 'center',
  },
  botonIniciar: { backgroundColor: colors.primary },
  botonDetener: { backgroundColor: colors.text },
  botonContenido: { flexDirection: 'row', alignItems: 'center', gap: space.sm },
  botonTexto: { color: '#FFFFFF', fontWeight: '800', fontSize: font.lg },
  botonSecundario: {
    minHeight: 44,
    marginTop: space.md,
    alignItems: 'center',
    justifyContent: 'center',
  },
  botonSecundarioTexto: {
    color: colors.textMuted,
    fontWeight: '700',
    fontSize: font.md,
  },
  pie: {
    color: colors.textMuted,
    fontSize: font.xs,
    textAlign: 'center',
    marginTop: space.xl,
  },
});
