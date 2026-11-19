import React, { useEffect, useRef, useState } from 'react';
import {
  ActivityIndicator,
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
    reconectar,
    reiniciarMapa,
  } = useRobotData();

  useEffect(() => {
    if (!aviso) return;
    const t = setTimeout(() => setAviso(null), 4000);
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
  const anchoMapa = Math.min(width - space.lg * 2, 760);

  // Parpadeo de pantalla completa mientras haya gas (requisito del proyecto).
  const pulso = useRef(new Animated.Value(0)).current;
  useEffect(() => {
    if (!gasAlerta) {
      pulso.stopAnimation();
      pulso.setValue(0);
      return;
    }
    const ciclo = Animated.loop(
      Animated.sequence([
        Animated.timing(pulso, { toValue: 1, duration: 420, useNativeDriver: true }),
        Animated.timing(pulso, { toValue: 0, duration: 420, useNativeDriver: true }),
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
            <View style={[styles.estado, conectado ? styles.estadoOk : styles.estadoOff]}>
              <View
                style={[
                  styles.punto,
                  { backgroundColor: conectado ? colors.success : colors.warning },
                ]}
              />
              <Text style={[styles.estadoTexto, conectado ? styles.estadoOkTexto : styles.estadoOffTexto]}>
                {conectado ? 'En línea' : 'Sin enlace'}
              </Text>
            </View>
          </View>

          {!!error && (
            <Banner tono="aviso" texto={error} accion="Reintentar" onAccion={reconectar} />
          )}
          {!conectado && !error && (
            <View style={[styles.banner, styles.bannerNeutro]}>
              <ActivityIndicator size="small" color={colors.primary} />
              <Text style={styles.bannerTexto}>Conectando con la telemetría…</Text>
            </View>
          )}
          {conectado && sinDatos && (
            <Banner
              tono="aviso"
              texto="Enlace listo. Esperando la primera lectura del ESP32."
            />
          )}
          {!!aviso && <Banner tono="info" texto={aviso} />}

          {data ? (
            <GasGauge
              valor={data.sensors.gas}
              alerta={data.sensors.gasAlerta}
              ppm={data.sensors.gasPpm}
            />
          ) : (
            <View style={styles.cargando}>
              <ActivityIndicator size="small" color={colors.primary} />
              <Text style={styles.cargandoTexto}>Cargando estado del sensor…</Text>
            </View>
          )}

          <Tarjeta
            titulo="Mapa de ruta"
            sub="El rojo marca únicamente donde se detectó gas"
          >
            <MapCanvas
              trajectory={trajectory}
              gasPoints={gasPoints}
              robot={data ? data.pose : null}
              width={anchoMapa}
              height={300}
              resetKey={resetContador}
            />
            <Text style={styles.pista}>
              Pellizca para acercar · arrastra para mover · doble toque para centrar
            </Text>
          </Tarjeta>

          <Tarjeta titulo="Distancias" sub="Ultrasónicos del robot">
            <View style={styles.metricas}>
              <Metrica etiqueta="Frente" valor={data?.sensors.front ?? 0} />
              <Metrica etiqueta="Izquierda" valor={data?.sensors.left ?? 0} />
              <Metrica etiqueta="Derecha" valor={data?.sensors.right ?? 0} />
            </View>
          </Tarjeta>

          <Tarjeta titulo="Posición" sub={`Último dato: ${ultimaHora}`}>
            <View style={styles.filas}>
              <Fila etiqueta="X" valor={data ? `${data.pose.x.toFixed(1)} cm` : '—'} />
              <Fila etiqueta="Y" valor={data ? `${data.pose.y.toFixed(1)} cm` : '—'} />
              <Fila
                etiqueta="Rumbo"
                valor={data ? `${data.pose.theta.toFixed(2)} rad` : '—'}
              />
            </View>
          </Tarjeta>

          <Tarjeta titulo="Control" sub="Las órdenes viajan a Supabase">
            <View style={styles.botones}>
              <Pressable
                style={[styles.boton, styles.botonIniciar]}
                onPress={() => enviar('ini')}
                accessibilityRole="button"
                accessibilityLabel="Iniciar el robot"
              >
                <Text style={styles.botonTexto}>▶  Iniciar</Text>
              </Pressable>
              <Pressable
                style={[styles.boton, styles.botonDetener]}
                onPress={() => enviar('alto')}
                accessibilityRole="button"
                accessibilityLabel="Detener el robot"
              >
                <Text style={styles.botonTexto}>■  Detener</Text>
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
          </Tarjeta>

          <Text style={styles.pie}>
            RMADG1 · telemetría en tiempo real con Supabase
          </Text>
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
            <Text style={styles.bandaTexto}>⚠  GAS DETECTADO · robot detenido</Text>
          </View>
        </>
      )}
    </View>
  );
}

function Tarjeta({
  titulo,
  sub,
  children,
}: {
  titulo: string;
  sub?: string;
  children: React.ReactNode;
}) {
  return (
    <View style={styles.tarjeta}>
      <Text style={styles.tarjetaTitulo}>{titulo}</Text>
      {!!sub && <Text style={styles.tarjetaSub}>{sub}</Text>}
      <View style={styles.tarjetaCuerpo}>{children}</View>
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
      <Text style={styles.metricaUnidad}>{activo ? 'cm' : 'sin eco'}</Text>
      <View style={styles.metricaPista}>
        <View style={[styles.metricaBarra, { width: `${pct}%` }]} />
      </View>
    </View>
  );
}

function Fila({ etiqueta, valor }: { etiqueta: string; valor: string }) {
  return (
    <View style={styles.fila}>
      <Text style={styles.filaEtiqueta}>{etiqueta}</Text>
      <Text style={styles.filaValor}>{valor}</Text>
    </View>
  );
}

function Banner({
  tono,
  texto,
  accion,
  onAccion,
}: {
  tono: 'aviso' | 'info';
  texto: string;
  accion?: string;
  onAccion?: () => void;
}) {
  return (
    <View style={[styles.banner, tono === 'aviso' ? styles.bannerAviso : styles.bannerInfo]}>
      <Text style={styles.bannerTexto}>{texto}</Text>
      {!!accion && !!onAccion && (
        <Pressable onPress={onAccion} accessibilityRole="button">
          <Text style={styles.bannerAccion}>{accion}</Text>
        </Pressable>
      )}
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
  content: {
    padding: space.lg,
    paddingBottom: space.xxl,
    alignItems: 'center',
  },
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
  bandaTexto: {
    color: '#FFFFFF',
    fontWeight: '800',
    fontSize: font.lg,
    letterSpacing: 0.4,
    textAlign: 'center',
  },
  header: {
    width: '100%',
    maxWidth: 760,
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    marginBottom: space.lg,
  },
  headerTexto: { flex: 1 },
  marca: {
    width: 48,
    height: 48,
    borderRadius: 14,
    backgroundColor: colors.primary,
    alignItems: 'center',
    justifyContent: 'center',
  },
  marcaTexto: { color: '#FFFFFF', fontWeight: '800', fontSize: font.lg },
  titulo: { fontSize: font.xxl, fontWeight: '800', color: colors.text, lineHeight: 32 },
  subtitulo: { fontSize: font.sm, color: colors.textMuted, marginTop: 2 },
  estado: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingHorizontal: 10,
    paddingVertical: 7,
    borderRadius: radius.pill,
  },
  estadoOk: { backgroundColor: colors.successSoft },
  estadoOff: { backgroundColor: colors.warningSoft },
  estadoOkTexto: { color: colors.success },
  estadoOffTexto: { color: colors.warning },
  punto: { width: 8, height: 8, borderRadius: 4 },
  estadoTexto: { fontSize: font.sm, fontWeight: '700' },
  banner: {
    width: '100%',
    maxWidth: 760,
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.sm,
    padding: space.md,
    borderRadius: radius.md,
    marginBottom: space.md,
  },
  bannerAviso: { backgroundColor: colors.warningSoft },
  bannerInfo: { backgroundColor: colors.primarySoft },
  bannerNeutro: { backgroundColor: colors.surface, borderWidth: 1, borderColor: colors.border },
  bannerTexto: { flex: 1, fontSize: font.md, color: colors.textSecondary },
  bannerAccion: { color: colors.primary, fontWeight: '800', fontSize: font.md },
  cargando: {
    width: '100%',
    maxWidth: 760,
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.sm,
    padding: space.lg,
    backgroundColor: colors.surface,
    borderRadius: radius.lg,
    borderWidth: 1,
    borderColor: colors.border,
    marginBottom: space.md,
  },
  cargandoTexto: { color: colors.textMuted, fontSize: font.md },
  tarjeta: {
    width: '100%',
    maxWidth: 760,
    backgroundColor: colors.surface,
    borderRadius: radius.lg,
    borderWidth: 1,
    borderColor: colors.border,
    padding: space.lg,
    marginBottom: space.md,
  },
  tarjetaTitulo: { fontSize: font.lg, fontWeight: '800', color: colors.text },
  tarjetaSub: { fontSize: font.sm, color: colors.textMuted, marginTop: 2 },
  tarjetaCuerpo: { marginTop: space.md, alignItems: 'center' },
  pista: {
    fontSize: font.sm,
    color: colors.textMuted,
    textAlign: 'center',
    marginTop: space.sm,
  },
  metricas: { flexDirection: 'row', gap: space.sm, width: '100%' },
  metrica: {
    flex: 1,
    backgroundColor: colors.surfaceSoft,
    borderRadius: radius.md,
    padding: space.md,
    alignItems: 'center',
  },
  metricaEtiqueta: { fontSize: font.sm, color: colors.textMuted, fontWeight: '700' },
  metricaValor: { fontSize: 26, fontWeight: '800', color: colors.text, marginTop: 4 },
  metricaUnidad: { fontSize: font.xs, color: colors.textMuted },
  metricaPista: {
    height: 4,
    width: '100%',
    borderRadius: 2,
    backgroundColor: colors.border,
    marginTop: space.sm,
    overflow: 'hidden',
  },
  metricaBarra: { height: 4, borderRadius: 2, backgroundColor: colors.primary },
  filas: { width: '100%' },
  fila: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: space.sm,
    borderBottomWidth: 1,
    borderBottomColor: colors.border,
  },
  filaEtiqueta: { color: colors.textMuted, fontSize: font.md },
  filaValor: { color: colors.text, fontWeight: '700', fontSize: font.md },
  botones: { flexDirection: 'row', gap: space.sm, width: '100%' },
  boton: {
    flex: 1,
    minHeight: 52,
    borderRadius: radius.md,
    alignItems: 'center',
    justifyContent: 'center',
  },
  botonIniciar: { backgroundColor: colors.primary },
  botonDetener: { backgroundColor: colors.text },
  botonTexto: { color: '#FFFFFF', fontWeight: '800', fontSize: font.lg },
  botonSecundario: {
    width: '100%',
    minHeight: 48,
    marginTop: space.sm,
    borderRadius: radius.md,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: colors.surfaceSoft,
    borderWidth: 1,
    borderColor: colors.border,
  },
  botonSecundarioTexto: {
    color: colors.textSecondary,
    fontWeight: '700',
    fontSize: font.md,
  },
  pie: {
    color: colors.textMuted,
    fontSize: font.sm,
    textAlign: 'center',
    marginTop: space.sm,
  },
});
