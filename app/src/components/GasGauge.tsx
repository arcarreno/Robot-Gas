import React from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { colors, font, space } from '../theme';
import { Icon } from './Icon';

type Props = {
  valor: number;
  alerta: boolean;
  ppm?: number;
};

export function GasGauge({ valor, alerta, ppm }: Props) {
  const hayPpm = typeof ppm === 'number' && ppm > 0;

  return (
    <View style={styles.fila} accessibilityLiveRegion="polite">
      <View style={[styles.icono, alerta ? styles.iconoAlerta : styles.iconoOk]}>
        <Icon
          nombre={alerta ? 'alerta' : 'check'}
          size={20}
          color={alerta ? colors.danger : colors.success}
        />
      </View>
      <View style={styles.info}>
        <Text style={[styles.estado, alerta ? styles.textoAlerta : styles.textoOk]}>
          {alerta ? 'GAS DETECTADO' : 'Aire limpio'}
        </Text>
        <Text style={styles.detalle}>
          {alerta
            ? 'El robot se detuvo por seguridad.'
            : hayPpm
              ? 'Sensor MQ-2 activo, sin gas detectado.'
              : 'Sin concentración analógica (DO digital).'}
        </Text>
      </View>
      <Text style={[styles.numero, alerta ? styles.textoAlerta : styles.textoOk]}>
        {hayPpm ? `${Math.round(ppm)} ppm` : `DO ${valor}`}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  fila: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    paddingVertical: space.sm,
  },
  icono: {
    width: 34,
    height: 34,
    borderRadius: 17,
    alignItems: 'center',
    justifyContent: 'center',
  },
  iconoOk: { backgroundColor: colors.successSoft },
  iconoAlerta: { backgroundColor: colors.dangerSoft },
  info: { flex: 1 },
  estado: { fontSize: font.lg, fontWeight: '800' },
  textoOk: { color: colors.success },
  textoAlerta: { color: colors.danger },
  detalle: { fontSize: font.sm, color: colors.textMuted, marginTop: 2 },
  numero: { fontSize: font.md, fontWeight: '800' },
});
