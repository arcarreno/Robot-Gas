/* =========================================================
   PLANTILLA DE SECRETOS
   Copia este archivo como "secrets.h" en la MISMA carpeta y
   rellena tus datos reales. "secrets.h" esta en .gitignore,
   asi que nunca se sube al repositorio.

   cp secrets.example.h secrets.h
   ========================================================= */

// --- WiFi: se prueban todas; gana la que este disponible ---
struct RedWiFi { const char* ssid; const char* pass; };
const RedWiFi REDES_WIFI[] = {
  {"TU_RED_WIFI",     "TU_CONTRASENA_WIFI"},
  // {"OTRA_RED",     "OTRA_CONTRASENA"},
};
const int NUM_REDES_WIFI = 1;

// --- Supabase ---
// Tabla de lecturas (telemetria)
const char* SUPABASE_URL = "https://TU_PROYECTO.supabase.co/rest/v1/lecturas_robot";
// Comandos sin procesar (el mas antiguo primero)
const char* SUPABASE_CMD_URL = "https://TU_PROYECTO.supabase.co/rest/v1/comandos_robot?procesado=eq.false&robot_id=eq.robot1&order=id.asc&limit=1";
// Base para marcar un comando como procesado
const char* SUPABASE_CMD_URL_BASE = "https://TU_PROYECTO.supabase.co/rest/v1/comandos_robot?id=eq.";
// Clave publishable (va en el header apikey)
const char* SUPABASE_KEY = "sb_publishable_TU_CLAVE";
