// Fernzugriff: Station <-> App unterwegs ueber einen MQTT-Broker im Internet.
// Alles ist mit AES-256-GCM verschluesselt (Schluessel = SHA-256 von APP_SCHLUESSEL).
// Der Broker sieht nur Zufallsdaten; ohne Schluessel kann niemand mitlesen oder Befehle schicken.
#pragma once
#include <Arduino.h>
#include <vector>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "esp_random.h"
#include "secrets.h"

#ifndef MQTT_HOST
#define MQTT_HOST "broker.hivemq.com"   // oeffentlich, ohne Konto (eigener Broker: siehe README)
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 8883
#endif
#ifndef MQTT_BENUTZER
#define MQTT_BENUTZER ""
#endif
#ifndef MQTT_PASSWORT
#define MQTT_PASSWORT ""
#endif

static uint8_t fernKey[32];

inline void fernInit() {
  mbedtls_sha256((const uint8_t*)APP_SCHLUESSEL, strlen(APP_SCHLUESSEL), fernKey, 0);
}

// Klartext -> base64( iv[12] | geheim | tag[16] )   (gleiches Format wie WebCrypto AES-GCM)
inline String fernVerpacken(const String& klar) {
  const size_t n = klar.length();
  std::vector<uint8_t> buf(12 + n + 16);
  esp_fill_random(buf.data(), 12);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, fernKey, 256);
  mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, buf.data(), 12, nullptr, 0,
                            (const uint8_t*)klar.c_str(), buf.data() + 12, 16, buf.data() + 12 + n);
  mbedtls_gcm_free(&g);
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, buf.data(), buf.size());
  std::vector<uint8_t> out(olen + 1);
  mbedtls_base64_encode(out.data(), out.size(), &olen, buf.data(), buf.size());
  out[olen] = 0;
  return String((const char*)out.data());
}

inline bool fernAuspacken(const uint8_t* b64, size_t len, String& klar) {
  std::vector<uint8_t> raw(len + 4);
  size_t olen = 0;
  if (mbedtls_base64_decode(raw.data(), raw.size(), &olen, b64, len) != 0 || olen < 12 + 16) return false;
  const size_t n = olen - 28;
  std::vector<uint8_t> pt(n + 1);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, fernKey, 256);
  int r = mbedtls_gcm_auth_decrypt(&g, n, raw.data(), 12, nullptr, 0, raw.data() + 12 + n, 16, raw.data() + 12, pt.data());
  mbedtls_gcm_free(&g);
  if (r != 0) return false;
  pt[n] = 0;
  klar = String((const char*)pt.data());
  return true;
}

// Themen: hundetracker/<MQTT_THEMA>/status | karte | befehl
inline String fernThema(const char* teil) { return String("hundetracker/") + MQTT_THEMA + "/" + teil; }
