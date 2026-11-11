/* =========================================================
   PLANTILLA DE SECRETOS
   Copia este archivo como "secrets.h" en la MISMA carpeta y
   rellena tus datos reales. "secrets.h" esta en .gitignore,
   asi que nunca se sube al repositorio.

   cp secrets.example.h secrets.h
   ========================================================= */

// --- WiFi ---
const char* WIFI_SSID = "TU_RED_WIFI";
const char* WIFI_PASS = "TU_CONTRASENA_WIFI";

// --- Supabase ---
// URL base del proyecto (sin /rest/v1/...)
const char* SUPABASE_BASE = "https://TU_PROYECTO.supabase.co";
// Host para el handshake TLS
const char* SUPABASE_HOST = "TU_PROYECTO.supabase.co";
// Endpoints REST
const char* SUPABASE_URL = "https://TU_PROYECTO.supabase.co/rest/v1/lecturas_robot";
const char* SUPABASE_CMD_URL = "https://TU_PROYECTO.supabase.co/rest/v1/comandos_robot?procesado=eq.false&robot_id=eq.robot1&order=id.asc&limit=1";
const char* SUPABASE_CMD_URL_BASE = "https://TU_PROYECTO.supabase.co/rest/v1/comandos_robot?id=eq.";
// Clave publishable (va en el header apikey)
const char* SUPABASE_KEY = "sb_publishable_TU_CLAVE";
