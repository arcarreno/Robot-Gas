import React, { useCallback, useEffect, useState } from 'react';
import {
  ActivityIndicator,
  FlatList,
  PermissionsAndroid,
  Platform,
  Pressable,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import type { BluetoothDevice } from 'react-native-bluetooth-classic';
import { dispositivosEmparejados } from '../services/bluetooth';

type Props = {
  onWiFi: () => void;
  onBluetooth: (address: string) => void;
};

async function pedirPermisos(): Promise<void> {
  if (Platform.OS !== 'android') {
    return;
  }
  if (Number(Platform.Version) >= 31) {
    const result = await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
    ]);
    if (
      result[PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT] !==
      PermissionsAndroid.RESULTS.GRANTED
    ) {
      throw new Error(
        'Concede "Dispositivos cercanos" a RobotCarr para ver y conectar el robot.',
      );
    }
  } else {
    const result = await PermissionsAndroid.request(
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
    );
    if (result !== PermissionsAndroid.RESULTS.GRANTED) {
      throw new Error(
        'Concede el permiso de ubicación para usar Bluetooth en este Android.',
      );
    }
  }
}

export function ConnectScreen({ onWiFi, onBluetooth }: Props) {
  const [devices, setDevices] = useState<BluetoothDevice[]>([]);
  const [cargando, setCargando] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const cargar = useCallback(async () => {
    setCargando(true);
    setError(null);
    try {
      await pedirPermisos();
      const list = await dispositivosEmparejados();
      setDevices(list);
    } catch (e: any) {
      setError(e?.message ?? 'No se pudo acceder al Bluetooth');
    } finally {
      setCargando(false);
    }
  }, []);

  useEffect(() => {
    cargar();
  }, [cargar]);

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Robot VIEP</Text>
      <Text style={styles.subtitle}>Selecciona cómo conectar</Text>

      <Pressable style={[styles.card, styles.wifi]} onPress={onWiFi}>
        <Text style={styles.cardTitle}>Wi-Fi · Supabase</Text>
        <Text style={styles.cardText}>
          Lee los datos en la nube en tiempo real. Requiere que el robot esté
          conectado a Wi-Fi.
        </Text>
      </Pressable>

      <View style={styles.separador} />

      <View style={styles.headerRow}>
        <Text style={styles.sectionTitle}>Bluetooth</Text>
        <Pressable onPress={cargar}>
          <Text style={styles.refresh}>Actualizar</Text>
        </Pressable>
      </View>

      {cargando && <ActivityIndicator style={{ marginTop: 12 }} />}
      {error && <Text style={styles.error}>{error}</Text>}

      {!cargando && devices.length === 0 && !error && (
        <Text style={styles.hint}>
          No hay dispositivos emparejados. Empareja el robot ({'RobotBT'}) desde
          los ajustes de Bluetooth de Android y vuelve a actualizar.
        </Text>
      )}

      <FlatList
        data={devices}
        keyExtractor={d => d.address}
        contentContainerStyle={{ paddingTop: 8 }}
        renderItem={({ item }) => (
          <Pressable
            style={styles.device}
            onPress={() => onBluetooth(item.address)}
          >
            <Text style={styles.deviceName}>{item.name || 'Sin nombre'}</Text>
            <Text style={styles.deviceAddress}>{item.address}</Text>
          </Pressable>
        )}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, padding: 20, backgroundColor: '#F8FAFC' },
  title: { fontSize: 28, fontWeight: '800', color: '#0F172A' },
  subtitle: { fontSize: 14, color: '#64748B', marginBottom: 20 },
  card: { borderRadius: 14, padding: 16 },
  wifi: { backgroundColor: '#A0C4FF' },
  cardTitle: { fontSize: 17, fontWeight: '700', color: '#0B2A4A' },
  cardText: { color: '#0B2A4A', marginTop: 6, fontSize: 13 },
  separador: { height: 1, backgroundColor: '#E2E8F0', marginVertical: 20 },
  headerRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  sectionTitle: { fontSize: 18, fontWeight: '700', color: '#0F172A' },
  refresh: { color: '#2563EB', fontWeight: '600' },
  hint: { color: '#64748B', marginTop: 12, fontSize: 13, lineHeight: 19 },
  error: { color: '#B91C1C', marginTop: 12, fontSize: 13 },
  device: {
    backgroundColor: '#FFFFFF',
    borderRadius: 12,
    padding: 14,
    marginTop: 8,
    borderWidth: 1,
    borderColor: '#E2E8F0',
  },
  deviceName: { fontWeight: '700', color: '#0F172A' },
  deviceAddress: { color: '#64748B', fontSize: 12, marginTop: 2 },
});
