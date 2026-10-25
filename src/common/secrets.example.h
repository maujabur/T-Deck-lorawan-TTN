#pragma once
//
// secrets.example.h  (TEMPLATE — versionado no git)
// --------------------------------------------------
// Copie este arquivo para "secrets.h" (na MESMA pasta) e preencha com os
// seus valores reais. O "secrets.h" está no .gitignore e NUNCA deve ser
// commitado — ele guarda credenciais de WiFi e chaves de API.
//
//   cp src/common/secrets.example.h src/common/secrets.h
//
// Depois edite src/common/secrets.h com os valores reais.
//

// ---- WiFi (hotspot do celular, sem captive portal) ----
#define WIFI_SSID        "SEU_SSID_AQUI"
#define WIFI_PASSWORD    "SUA_SENHA_AQUI"

// ---- ThingSpeak ----
// Write API Key do seu canal (Channel -> API Keys -> Write API Key).
#define THINGSPEAK_WRITE_KEY   "SUA_WRITE_API_KEY_AQUI"
#define THINGSPEAK_CHANNEL_ID  0
