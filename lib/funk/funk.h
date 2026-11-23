// Funk-Pakete Halsband <-> Station (LoRa 869,525 MHz) mit Pruefsumme (HMAC-SHA256, 4 Byte).
// Ohne den gemeinsamen Schluessel (secrets.h) kann niemand Pakete faelschen.
#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include "mbedtls/md.h"
#include "grenze.h"

// Heltec Wireless Tracker V1.1 – SX1262
#define LORA_NSS 8
#define LORA_SCK 9
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_RST 12
#define LORA_BUSY 13
#define LORA_DIO1 14

// 869,4–869,65 MHz: 10 % Sendezeit erlaubt (EU), passt fuer Sekundentakt
#define FUNK_FREQ 869.525
#define FUNK_BW 125.0
#define FUNK_SF 7
#define FUNK_CR 5
#define FUNK_SYNC 0x2B
#define FUNK_DBM 14

enum : uint8_t { P_POSITION = 1, P_ANTWORT = 2 };
enum : uint8_t { B_NICHTS = 0, B_TEST = 1, B_PAUSE = 2, B_PAUSE_AUS = 3 };
enum : uint8_t { F_FIX = 1, F_PAUSE = 2, F_ALARM = 4 };

#pragma pack(push, 1)
struct Kopf { uint8_t m1 = 'H', m2 = 'T', typ; uint16_t seq; };
struct Position {
  Kopf k;
  int32_t lat, lon;     // Grad * 1e7
  int16_t abstand_dm;   // > 0 draussen
  uint8_t zustand;      // DRINNEN/DRAUSSEN
  uint8_t flags;        // F_*
  uint16_t akku_mv;
  uint8_t sats, hdop10;
  uint16_t grenzVersion;
  uint16_t pauseMin;    // Rest der Spaziergang-Pause
};
struct Antwort {
  Kopf k;               // seq = seq der beantworteten Position (Schutz gegen Wiederholung)
  uint8_t befehl;       // B_*
  uint16_t param;       // z. B. Pause-Minuten
  uint16_t grenzVersion;
  uint8_t n;            // 0 = keine Punkte mitgeschickt
  int32_t pkt[GRENZE_MAX_PUNKTE * 2];
};
#pragma pack(pop)

static const uint8_t MAC_LEN = 4;

inline void funkMac(const char* schluessel, const uint8_t* d, size_t n, uint8_t* out) {
  uint8_t h[32];
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(md, (const uint8_t*)schluessel, strlen(schluessel), d, n, h);
  memcpy(out, h, MAC_LEN);
}

// haengt die Pruefsumme an, Rueckgabe = Gesamtlaenge
inline size_t funkSiegeln(const char* key, uint8_t* buf, size_t n) {
  funkMac(key, buf, n, buf + n);
  return n + MAC_LEN;
}

// prueft Kennung + Pruefsumme, Rueckgabe = Nutzlaenge (0 = ungueltig)
inline size_t funkPruefen(const char* key, const uint8_t* buf, size_t n) {
  if (n < sizeof(Kopf) + MAC_LEN || buf[0] != 'H' || buf[1] != 'T') return 0;
  uint8_t m[MAC_LEN];
  funkMac(key, buf, n - MAC_LEN, m);
  uint8_t diff = 0;
  for (int i = 0; i < MAC_LEN; i++) diff |= m[i] ^ buf[n - MAC_LEN + i];
  return diff ? 0 : n - MAC_LEN;
}

// Empfangs-Flag aus dem DIO1-Interrupt
static volatile bool funkFlag = false;
#if defined(ESP32)
static void IRAM_ATTR funkIsr() { funkFlag = true; }
#endif

inline bool funkStart(SX1262& radio) {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int st = radio.begin(FUNK_FREQ, FUNK_BW, FUNK_SF, FUNK_CR, FUNK_SYNC, FUNK_DBM, 8, 1.8);
  if (st != RADIOLIB_ERR_NONE) { Serial.printf("LoRa Fehler %d\n", st); return false; }
  radio.setDio2AsRfSwitch(true);
  radio.setCRC(true);
  radio.setPacketReceivedAction(funkIsr);
  return true;
}
