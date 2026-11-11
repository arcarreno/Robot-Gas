// Credenciales del proyecto Supabase.
// Copia config.example.ts como config.ts y rellena tus datos.
// La clave "publishable" de Supabase esta pensada para ir en el cliente
// (la protege el RLS), pero por limpieza este archivo queda fuera del repo.
export const SUPABASE_URL = 'https://TU_PROYECTO.supabase.co';
export const SUPABASE_ANON_KEY = 'sb_publishable_TU_CLAVE';

// Identificador del robot en la base de datos
export const ROBOT_ID = 'robot1';

// Nombre Bluetooth del ESP32 (BluetoothSerial.begin("RobotBT"))
export const BLUETOOTH_NAME = 'RobotBT';
