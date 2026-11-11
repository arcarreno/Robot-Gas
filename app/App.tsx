import 'react-native-gesture-handler';
import React, { useState } from 'react';
import { GestureHandlerRootView } from 'react-native-gesture-handler';
import { SafeAreaProvider } from 'react-native-safe-area-context';
import { StatusBar } from 'react-native';
import { ConnectScreen } from './src/screens/ConnectScreen';
import { DashboardScreen } from './src/screens/DashboardScreen';

type Conexion =
  | { modo: 'wifi' }
  | { modo: 'bluetooth'; address: string }
  | null;

export default function App() {
  const [conexion, setConexion] = useState<Conexion>(null);

  return (
    <GestureHandlerRootView style={{ flex: 1 }}>
      <SafeAreaProvider>
        <StatusBar barStyle="dark-content" />
        {conexion ? (
          <DashboardScreen
            modo={conexion.modo}
            address={conexion.modo === 'bluetooth' ? conexion.address : undefined}
            onSalir={() => setConexion(null)}
          />
        ) : (
          <ConnectScreen
            onWiFi={() => setConexion({ modo: 'wifi' })}
            onBluetooth={address => setConexion({ modo: 'bluetooth', address })}
          />
        )}
      </SafeAreaProvider>
    </GestureHandlerRootView>
  );
}
