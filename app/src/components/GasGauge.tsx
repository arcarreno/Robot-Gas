import React from 'react';
import { StyleSheet, Text, View } from 'react-native';

type Props = {
  valor: number;
  alerta: boolean;
  ppm?: number;
};

// Umbrales del firmware (MQ2_PPM_ALARMA / MQ2_PPM_LIMPIO)
const PPM_ALARMA = 300;
const PPM_LIMPIO = 200;

export function GasGauge({ valor, alerta, ppm }: Props) {
  const tienePpm = typeof ppm === 'number' && ppm >= 0;

  // Barra de 0 a 500 ppm (el tope útil está alrededor de 500).
  const pct = tienePpm ? Math.min(100, Math.max(0, (ppm / 500) * 100)) : 0;
  const colorBarra = alerta ? '#B91C1C' : pct > 60 ? '#D97706' : '#16A34A';

  return (
    <View style={[styles.box, alerta ? styles.boxAlerta : styles.boxOk]}>
      <View style={styles.row}>
        <Text style={[styles.estado, alerta ? styles.textoAlerta : styles.textoOk]}>
          {alerta ? '⚠ GAS DETECTADO' : 'Aire limpio'}
        </Text>
        <Text style={styles.valor}>
          {tienePpm ? `${Math.round(ppm)} ppm` : `DO: ${valor}`}
        </Text>
      </View>

      {tienePpm && (
        <>
          <View style={styles.pista}>
            <View
              style={[styles.relleno, { width: `${pct}%`, backgroundColor: colorBarra }]}
            />
          </View>
          <View style={styles.escala}>
            <Text style={styles.marca}>0</Text>
            <Text style={styles.marca}>alarma {PPM_ALARMA}</Text>
            <Text style={styles.marca}>500</Text>
          </View>
          <Text style={styles.detalle}>
            {alerta
              ? `Concentración alta (${Math.round(ppm)} ppm). El robot se detuvo y queda enclavado hasta 'gascal' o 'gasoff'.`
              : ppm >= PPM_LIMPIO
                ? `Concentración en ${Math.round(ppm)} ppm, cerca del umbral de alarma (${PPM_ALARMA} ppm).`
                : `Concentración ${Math.round(ppm)} ppm. Umbral de alarma ${PPM_ALARMA} ppm, se limpia por debajo de ${PPM_LIMPIO} ppm.`}
          </Text>
        </>
      )}

      {!tienePpm && (
        <Text style={styles.detalle}>
          {alerta
            ? 'El robot se detuvo automáticamente por seguridad.'
            : 'Sin lectura de concentración todavía (MQ-2 calibrándose).'}
        </Text>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  box: {
    borderRadius: 12,
    padding: 12,
    marginVertical: 8,
  },
  boxOk: { backgroundColor: '#E0F2FE' },
  boxAlerta: { backgroundColor: '#FEE2E2' },
  row: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  estado: { fontWeight: '700', fontSize: 15 },
  textoOk: { color: '#0369A1' },
  textoAlerta: { color: '#B91C1C' },
  valor: { color: '#334155', fontWeight: '600' },
  pista: {
    height: 8,
    borderRadius: 4,
    backgroundColor: '#CBD5E1',
    marginTop: 8,
    overflow: 'hidden',
  },
  relleno: { height: 8, borderRadius: 4 },
  escala: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: 2,
  },
  marca: { color: '#64748B', fontSize: 10 },
  detalle: { color: '#475569', marginTop: 4, fontSize: 12 },
});
