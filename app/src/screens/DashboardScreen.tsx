import React, { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Animated,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  useWindowDimensions,
} from 'react-native';
import { MapCanvas } from '../components/MapCanvas';
import { GasGauge } from '../components/GasGauge';
import { ModoConexion, useRobotData } from '../hooks/useRobotData';
import { supabase } from '../lib/supabase';
import { enviarComando } from '../services/bluetooth';
import { ROBOT_ID } from '../config';

type Props = {
  modo: ModoConexion;
  address?: string;
  onSalir: () => void;
};

export function DashboardScreen({ modo, address, onSalir }: Props) {
  const { width } = useWindowDimensions();
  const [comando, setComando] = useState('');
  const {
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
  } = useRobotData(modo);

  useEffect(() => {
    if (modo === 'bluetooth' && address) {
      conectarBluetooth(address);
    }
    return () => {
      if (modo === 'bluetooth') {
        desconectarBluetooth();
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [modo, address]);

  async function enviar(cmd: string) {
    try {
      if (modo === 'bluetooth') {
        await enviarComando(btRef.current, cmd);
      } else {
        const { error: err } = await supabase
          .from('comandos_robot')
          .insert({ comando: cmd, robot_id: ROBOT_ID });
        if (err) {
          throw err;
        }
      }
    } catch (e: any) {
      Alert.alert('Error', e?.message ?? 'No se pudo enviar el comando');
    }
  }

  const anchoMapa = width - 32;
  const gasAlerta = !!data?.sensors.gasAlerta;

  // Parpadeo de pantalla completa mientras haya gas.
  const pulso = useRef(new Animated.Value(0)).current;
  useEffect(() => {
    if (!gasAlerta) {
      pulso.stopAnimation();
      pulso.setValue(0);
      return;
    }
    const ciclo = Animated.loop(
      Animated.sequence([
        Animated.timing(pulso, {
          toValue: 1,
          duration: 400,
          useNativeDriver: true,
        }),
        Animated.timing(pulso, {
          toValue: 0,
          duration: 400,
          useNativeDriver: true,
        }),
      ]),
    );
    ciclo.start();
    return () => ciclo.stop();
  }, [gasAlerta, pulso]);

  return (
    <View style={styles.root}>
      <ScrollView style={styles.container} contentContainerStyle={styles.content}>
        <View style={styles.header}>
          <View>
            <Text style={styles.title}>Monitoreo</Text>
            <Text style={styles.modo}>
              {modo === 'wifi' ? 'Wi-Fi · Supabase Realtime' : 'Bluetooth directo'}
            </Text>
          </View>
          <Pressable onPress={onSalir} style={styles.salir}>
            <Text style={styles.salirTexto}>Salir</Text>
          </Pressable>
        </View>

        <View style={styles.estadoFila}>
          <View
            style={[
              styles.punto,
              { backgroundColor: conectado ? '#22C55E' : '#EF4444' },
            ]}
          />
          <Text style={styles.estadoTexto}>
            {conectado ? 'Conectado' : 'Sin conexión'}
          </Text>
        </View>

        {error && <Text style={styles.error}>{error}</Text>}
        {conectado && sinDatos && (
          <Text style={styles.aviso}>
            Enlace activo, pero el robot no envía datos. Revisa que el ESP32 esté
            encendido y sin otra conexión Bluetooth abierta.
          </Text>
        )}

        <MapCanvas
          trajectory={trajectory}
          obstacles={obstacles}
          robot={data ? data.pose : null}
          width={anchoMapa}
          height={340}
          resetKey={resetContador}
        />
        <Text style={styles.pista}>
          Pellizca para zoom · arrastra para mover · doble toque para reiniciar
        </Text>

        {data && (
          <GasGauge
            valor={data.sensors.gas}
            alerta={data.sensors.gasAlerta}
            ppm={data.sensors.gasPpm}
          />
        )}

        {data && (
          <View style={styles.tarjeta}>
            <Fila etiqueta="X" valor={`${data.pose.x.toFixed(1)} cm`} />
            <Fila etiqueta="Y" valor={`${data.pose.y.toFixed(1)} cm`} />
            <Fila etiqueta="θ" valor={`${data.pose.theta.toFixed(2)} rad`} />
            <Fila etiqueta="Frente" valor={`${data.sensors.front.toFixed(1)} cm`} />
            <Fila etiqueta="Izquierda" valor={`${data.sensors.left.toFixed(1)} cm`} />
            <Fila etiqueta="Derecha" valor={`${data.sensors.right.toFixed(1)} cm`} />
          </View>
        )}

        <View style={styles.botones}>
          <Pressable style={[styles.boton, styles.iniciar]} onPress={() => enviar('ini')}>
            <Text style={styles.botonTexto}>Iniciar</Text>
          </Pressable>
          <Pressable style={[styles.boton, styles.detener]} onPress={() => enviar('alto')}>
            <Text style={styles.botonTexto}>Detener</Text>
          </Pressable>
          <Pressable style={[styles.boton, styles.reset]} onPress={reiniciarMapa}>
            <Text style={[styles.botonTexto, { color: '#0F172A' }]}>Limpiar mapa</Text>
          </Pressable>
        </View>

        {modo === 'bluetooth' && (
          <View style={styles.consolaCaja}>
            <Text style={styles.consolaTitulo}>Comandos del robot</Text>
            <View style={styles.comandoFila}>
              <TextInput
                style={styles.comandoInput}
                value={comando}
                onChangeText={setComando}
                placeholder="diag · net · gasinv · mq2"
                placeholderTextColor="#94A3B8"
                autoCapitalize="none"
                autoCorrect={false}
                returnKeyType="send"
                onSubmitEditing={() => {
                  const c = comando.trim();
                  if (c) {
                    enviar(c);
                    setComando('');
                  }
                }}
              />
              <Pressable
                style={styles.comandoBoton}
                onPress={() => {
                  const c = comando.trim();
                  if (c) {
                    enviar(c);
                    setComando('');
                  }
                }}
              >
                <Text style={styles.comandoBotonTexto}>Enviar</Text>
              </Pressable>
            </View>
            <View style={styles.chips}>
              {['diag', 'net', 'gasinv', 'gason', 'mq2'].map(c => (
                <Pressable key={c} style={styles.chip} onPress={() => enviar(c)}>
                  <Text style={styles.chipTexto}>{c}</Text>
                </Pressable>
              ))}
            </View>
          </View>
        )}

        {log.length > 0 && (
          <View style={styles.consola}>
            <Text style={styles.consolaTitulo}>Registro del enlace</Text>
            {log.map((l, i) => (
              <Text key={i} style={styles.consolaLinea} numberOfLines={2}>
                {l}
              </Text>
            ))}
          </View>
        )}
      </ScrollView>

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
                  outputRange: [0.07, 0.4],
                }),
              },
            ]}
          />
          <View pointerEvents="none" style={styles.alertaBanda}>
            <Text style={styles.alertaTexto}>⚠ GAS DETECTADO · robot detenido</Text>
          </View>
        </>
      )}
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

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: '#F8FAFC' },
  brillo: { backgroundColor: '#EF4444' },
  alertaBanda: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    backgroundColor: '#B91C1C',
    paddingVertical: 10,
    paddingHorizontal: 16,
    alignItems: 'center',
  },
  alertaTexto: {
    color: '#FFFFFF',
    fontWeight: '800',
    fontSize: 15,
    letterSpacing: 0.4,
  },
  container: { flex: 1, backgroundColor: '#F8FAFC' },
  content: { padding: 16, paddingBottom: 40 },
  header: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  title: { fontSize: 26, fontWeight: '800', color: '#0F172A' },
  modo: { fontSize: 13, color: '#64748B', marginTop: 2 },
  salir: {
    backgroundColor: '#E2E8F0',
    paddingHorizontal: 14,
    paddingVertical: 8,
    borderRadius: 10,
  },
  salirTexto: { fontWeight: '700', color: '#0F172A' },
  estadoFila: { flexDirection: 'row', alignItems: 'center', marginVertical: 12 },
  punto: { width: 10, height: 10, borderRadius: 5, marginRight: 8 },
  estadoTexto: { color: '#475569', fontWeight: '600' },
  error: { color: '#B91C1C', marginBottom: 8 },
  aviso: {
    color: '#92400E',
    backgroundColor: '#FEF3C7',
    borderRadius: 8,
    padding: 10,
    marginBottom: 8,
    fontSize: 13,
  },
  consola: {
    marginTop: 16,
    backgroundColor: '#0F172A',
    borderRadius: 10,
    padding: 10,
  },
  consolaCaja: {
    marginTop: 16,
    backgroundColor: '#FFFFFF',
    borderRadius: 10,
    padding: 10,
    borderWidth: 1,
    borderColor: '#E2E8F0',
  },
  comandoFila: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  comandoInput: {
    flex: 1,
    borderWidth: 1,
    borderColor: '#CBD5E1',
    borderRadius: 8,
    paddingHorizontal: 10,
    paddingVertical: 8,
    color: '#0F172A',
  },
  comandoBoton: {
    backgroundColor: '#0F172A',
    borderRadius: 8,
    paddingHorizontal: 14,
    paddingVertical: 9,
  },
  comandoBotonTexto: { color: '#FFFFFF', fontWeight: '700' },
  chips: { flexDirection: 'row', flexWrap: 'wrap', gap: 6, marginTop: 8 },
  chip: {
    backgroundColor: '#E2E8F0',
    borderRadius: 999,
    paddingHorizontal: 10,
    paddingVertical: 5,
  },
  chipTexto: { color: '#0F172A', fontSize: 12, fontWeight: '600' },
  consolaTitulo: {
    color: '#94A3B8',
    fontSize: 11,
    fontWeight: '700',
    marginBottom: 6,
    textTransform: 'uppercase',
  },
  consolaLinea: {
    color: '#E2E8F0',
    fontFamily: 'monospace',
    fontSize: 11,
    marginBottom: 2,
  },
  pista: {
    fontSize: 12,
    color: '#64748B',
    textAlign: 'center',
    marginTop: 4,
    marginBottom: 8,
  },
  tarjeta: {
    backgroundColor: '#FFFFFF',
    borderRadius: 12,
    padding: 14,
    marginVertical: 8,
  },
  fila: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: 4,
  },
  filaEtiqueta: { color: '#64748B' },
  filaValor: { color: '#0F172A', fontWeight: '600' },
  botones: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: 16,
    gap: 8,
  },
  boton: {
    flex: 1,
    paddingVertical: 12,
    borderRadius: 10,
    alignItems: 'center',
  },
  iniciar: { backgroundColor: '#22C55E' },
  detener: { backgroundColor: '#EF4444' },
  reset: { backgroundColor: '#E2E8F0' },
  botonTexto: { color: '#FFFFFF', fontWeight: '700' },
});
