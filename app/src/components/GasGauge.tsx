import React from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { colors, radius, space } from '../theme';

type Props = {
  valor: number;
  alerta: boolean;
  ppm?: number;
};

export function GasGauge({ valor, alerta, ppm }: Props) {
  // El modulo instalado entrega el DO digital y no entrega concentration
  // analogica util: 0 ppm no significa "sin sensor", significa "sin AO".
  const hayPpm = typeof ppm === 'number' && ppm > 0;

  return (
    <View
      style={[styles.card, alerta ? styles.cardAlerta : styles.cardOk]}
      accessibilityRole="text"
      accessibilityLiveRegion="polite"
    >
      <View style={[styles.icono, alerta ? styles.iconoAlerta : styles.iconoOk]}>
        <Text style={styles.iconoTexto}>{alerta ? '⚠' : '✓'}</Text>
      </View>

      <View style={styles.info}>
        <Text style={[styles.titulo, alerta ? styles.textoAlerta : styles.textoOk]}>
          {alerta ? 'GAS DETECTADO' : 'AIRE LIMPIO'}
        </Text>
        <Text style={styles.detalle}>
          {alerta
            ? 'El robot se detuvo por seguridad. Ventila la zona antes de reiniciarlo.'
            : 'Sensor MQ-2 activo. Sin gas detectado.'}
        </Text>
      </View>

      <View style={styles.medida}>
        <Text style={styles.numero}>{hayPpm ? Math.round(ppm as number) : valor}</Text>
        <Text style={styles.unidad}>{hayPpm ? 'ppm' : 'DO'}</Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  card: {
    width: '100%',
    maxWidth: 760,
    flexDirection: 'row',
    alignItems: 'center',
    gap: space.md,
    padding: space.lg,
    borderRadius: radius.lg,
    borderWidth: 1,
    marginBottom: space.md,
  },
  cardOk: { backgroundColor: colors.successSoft, borderColor: '#BBF7D0' },
  cardAlerta: { backgroundColor: colors.dangerSoft, borderColor: '#FECACA' },
  icono: {
    width: 44,
    height: 44,
    borderRadius: 14,
    alignItems: 'center',
    justifyContent: 'center',
  },
  iconoOk: { backgroundColor: colors.success },
  iconoAlerta: { backgroundColor: colors.danger },
  iconoTexto: { color: '#FFFFFF', fontSize: 22, fontWeight: '800' },
  info: { flex: 1 },
  titulo: { fontSize: 16, fontWeight: '800', letterSpacing: 0.3 },
  textoOk: { color: colors.success },
  textoAlerta: { color: colors.danger },
  detalle: {
    fontSize: 12,
    color: colors.textSecondary,
    marginTop: 4,
    lineHeight: 17,
  },
  medida: { alignItems: 'flex-end' },
  numero: { fontSize: 24, fontWeight: '800', color: colors.text },
  unidad: { fontSize: 11, color: colors.textMuted, fontWeight: '700' },
});
