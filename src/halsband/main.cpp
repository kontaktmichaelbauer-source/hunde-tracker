// HALSBAND – Heltec Wireless Tracker V1.1
// GPS 1x pro Sekunde -> Grenze pruefen -> draussen: Ton + Vibration, sofort Funk an die Station.
// Wieder drinnen: Ton + Vibration aus.
#include <Arduino.h>
#include <Preferences.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include "grenze.h"
#include "funk.h"
#include "secrets.h"

// ---- Pins (Heltec Wireless Tracker V1.1) ----
#define VEXT_CTRL 3      // HIGH = GPS + Display mit Strom
#define GPS_RX 33        // ESP empfaengt (TX des GPS)
#define GPS_TX 34
#define GPS_RST 35
#define TFT_LED 21       // Display-Licht aus lassen (Strom sparen)
#define ADC_CTRL 2       // HIGH = Akku-Messung an
#define ADC_AKKU 1
#define LED_PIN 18
#define TASTE 0          // Taste "USER": 2 s halten = Spaziergang-Pause an/aus
#define TON_PIN 5        // -> MOSFET -> Sirene (12 V ueber Step-up)
#define VIB_PIN 6        // -> MOSFET -> Vibrationsmotor

// ---- Einstellungen ----
static const uint32_t SENDEN_DRINNEN_MS = 10000;  // Lebenszeichen
static const uint32_t SENDEN_DRAUSSEN_MS = 3000;  // waehrend Alarm
static const uint8_t MIN_SATS = 5;
static const float MAX_HDOP = 4.0f;
static const uint16_t PAUSE_STANDARD_MIN = 60;

TinyGPSPlus gps;
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
Preferences prefs;
Grenze grenze;
Waechter waechter;
uint16_t seq = 0;
uint32_t letztesSenden = 0, pauseBis = 0, testBis = 0;
bool unbestaetigt = false;   // Zustandswechsel, Station hat noch nicht quittiert
float abstand = 0;

// ---------- Ton + Vibration ----------
void ausgaenge(bool an, uint32_t t) {
  // Ton: 700 ms an / 150 ms aus (Sirene), Vibration: 900 an / 300 aus
  bool ton = an && (t % 850) < 700;
  bool vib = an && (t % 1200) < 900;
  digitalWrite(TON_PIN, ton);
  digitalWrite(VIB_PIN, vib);
  digitalWrite(LED_PIN, an && (t % 400) < 200);
}

void piep(int mal, int ms) {   // kurze Rueckmeldung (Taste)
  for (int i = 0; i < mal; i++) {
    digitalWrite(TON_PIN, 1); digitalWrite(VIB_PIN, 1); delay(ms);
    digitalWrite(TON_PIN, 0); digitalWrite(VIB_PIN, 0); delay(ms);
  }
}

bool pauseAktiv() { return pauseBis && (int32_t)(pauseBis - millis()) > 0; }

// ---------- Grenze speichern/laden ----------
void grenzeLaden() {
  prefs.begin("hund", true);
  if (prefs.getBytesLength("grenze") == sizeof(Grenze)) prefs.getBytes("grenze", &grenze, sizeof(Grenze));
  prefs.end();
  Serial.printf("Grenze v%u, %u Punkte\n", grenze.version, grenze.n);
}
void grenzeSpeichern() {
  prefs.begin("hund", false);
  prefs.putBytes("grenze", &grenze, sizeof(Grenze));
  prefs.end();
}

uint16_t akkuMv() {
  digitalWrite(ADC_CTRL, HIGH);
  delay(2);
  uint32_t mv = analogReadMilliVolts(ADC_AKKU);
  digitalWrite(ADC_CTRL, LOW);
  return (uint16_t)(mv * 4.9f);   // Teiler 390k/100k
}

// ---------- Funk ----------
void antwortVerarbeiten(const Antwort& a, size_t len) {
  if (a.k.typ != P_ANTWORT || a.k.seq != seq) return;    // nur Antwort auf DIESES Paket
  unbestaetigt = false;
  if (a.n >= 3 && a.n <= GRENZE_MAX_PUNKTE && a.grenzVersion != grenze.version &&
      len >= offsetof(Antwort, pkt) + a.n * 8) {
    grenze.n = a.n;
    grenze.version = a.grenzVersion;
    for (int i = 0; i < a.n; i++) { grenze.lat[i] = a.pkt[2 * i]; grenze.lon[i] = a.pkt[2 * i + 1]; }
    grenzeSpeichern();
    waechter.zustand = DRINNEN; waechter.zaehler = 0;
    Serial.printf("Neue Grenze v%u (%u Punkte)\n", grenze.version, grenze.n);
    piep(1, 80);
  } else if (a.n == 0 && a.grenzVersion != grenze.version && a.grenzVersion != 0xFFFF) {
    // Station hat die Grenze geloescht
    grenze.n = 0; grenze.version = a.grenzVersion; grenzeSpeichern();
    waechter.zustand = DRINNEN;
  }
  if (a.befehl == B_TEST) testBis = millis() + 3000;
  if (a.befehl == B_PAUSE) { pauseBis = millis() + (uint32_t)(a.param ? a.param : PAUSE_STANDARD_MIN) * 60000UL; piep(2, 80); }
  if (a.befehl == B_PAUSE_AUS) { pauseBis = 0; piep(1, 200); }
}

void senden() {
  Position p{};
  p.k.typ = P_POSITION;
  p.k.seq = ++seq;
  bool fix = gps.location.isValid() && gps.location.age() < 3000;
  p.lat = fix ? (int32_t)llround(gps.location.lat() * 1e7) : 0;
  p.lon = fix ? (int32_t)llround(gps.location.lng() * 1e7) : 0;
  float a = constrain(abstand * 10.0f, -32000.0f, 32000.0f);
  p.abstand_dm = (int16_t)a;
  p.zustand = waechter.zustand;
  p.flags = (fix ? F_FIX : 0) | (pauseAktiv() ? F_PAUSE : 0) | (waechter.alarm(millis()) ? F_ALARM : 0);
  p.akku_mv = akkuMv();
  p.sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
  p.hdop10 = gps.hdop.isValid() ? (uint8_t)min(255.0, gps.hdop.hdop() * 10) : 255;
  p.grenzVersion = grenze.version;
  p.pauseMin = pauseAktiv() ? (uint16_t)((pauseBis - millis()) / 60000UL + 1) : 0;

  uint8_t buf[sizeof(Position) + MAC_LEN];
  memcpy(buf, &p, sizeof(p));
  size_t n = funkSiegeln(FUNK_SCHLUESSEL, buf, sizeof(p));
  radio.transmit(buf, n);
  letztesSenden = millis();

  // kurz auf Antwort horchen (Quittung, Befehle, neue Grenze)
  funkFlag = false;
  radio.startReceive();
  uint32_t t0 = millis();
  while (millis() - t0 < 600) {
    if (funkFlag) {
      funkFlag = false;
      static uint8_t rx[sizeof(Antwort) + MAC_LEN];
      size_t len = radio.getPacketLength();
      if (len <= sizeof(rx) && radio.readData(rx, len) == RADIOLIB_ERR_NONE) {
        size_t nutz = funkPruefen(FUNK_SCHLUESSEL, rx, len);
        if (nutz) {
          Antwort ant{};
          memcpy(&ant, rx, min(nutz, sizeof(ant)));
          antwortVerarbeiten(ant, nutz);
        }
      }
      break;
    }
    // Ton/Vibration laufen waehrend des Wartens weiter
    ausgaenge((waechter.alarm(millis()) && !pauseAktiv()) || millis() < testBis, millis());
    delay(5);
  }
  radio.standby();
}

// ---------- Taste ----------
void tasteAbfragen() {
  static uint32_t gedrueckt = 0;
  static bool erledigt = false;
  if (digitalRead(TASTE) == LOW) {
    if (!gedrueckt) gedrueckt = millis();
    if (!erledigt && millis() - gedrueckt > 2000) {
      erledigt = true;
      if (pauseAktiv()) { pauseBis = 0; piep(1, 300); }
      else { pauseBis = millis() + PAUSE_STANDARD_MIN * 60000UL; piep(2, 100); }
      senden();
    }
  } else { gedrueckt = 0; erledigt = false; }
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);         // reicht, spart Strom
  WiFi.mode(WIFI_OFF);
  btStop();
  pinMode(TON_PIN, OUTPUT); pinMode(VIB_PIN, OUTPUT); pinMode(LED_PIN, OUTPUT);
  pinMode(ADC_CTRL, OUTPUT); pinMode(TFT_LED, OUTPUT); digitalWrite(TFT_LED, LOW);
  pinMode(TASTE, INPUT_PULLUP);
  pinMode(VEXT_CTRL, OUTPUT); digitalWrite(VEXT_CTRL, HIGH);
  pinMode(GPS_RST, OUTPUT); digitalWrite(GPS_RST, HIGH);
  Serial1.begin(115200, SERIAL_8N1, GPS_RX, GPS_TX);

  grenzeLaden();
  if (!funkStart(radio)) { while (true) { piep(3, 100); delay(2000); } }
  piep(1, 60);
  Serial.println("Halsband bereit");
}

void loop() {
  while (Serial1.available()) gps.encode(Serial1.read());

  static uint32_t letzteMessung = 0;
  const uint32_t jetzt = millis();
  if (jetzt - letzteMessung >= 1000) {
    letzteMessung = jetzt;
    bool gueltig = gps.location.isValid() && gps.location.age() < 2000 &&
                   gps.satellites.value() >= MIN_SATS && gps.hdop.hdop() <= MAX_HDOP && grenze.n >= 3;
    if (gueltig) abstand = grenzAbstand(grenze, gps.location.lat(), gps.location.lng());
    if (waechter.messung(gueltig, abstand, jetzt)) {
      Serial.printf("Zustand -> %s (%.1f m)\n", waechter.zustand == DRAUSSEN ? "DRAUSSEN" : "DRINNEN", abstand);
      unbestaetigt = true;
      ausgaenge(waechter.alarm(jetzt) && !pauseAktiv(), jetzt);  // Ton sofort, noch vor dem Funk
      senden();
    }
  }

  const bool alarm = waechter.alarm(jetzt) && !pauseAktiv();
  ausgaenge(alarm || jetzt < testBis, jetzt);

  uint32_t takt = unbestaetigt ? 2000 : (waechter.zustand == DRAUSSEN ? SENDEN_DRAUSSEN_MS : SENDEN_DRINNEN_MS);
  if (millis() - letztesSenden >= takt) senden();

  tasteAbfragen();
  if (pauseBis && !pauseAktiv()) { pauseBis = 0; piep(1, 300); }   // Pause abgelaufen
  delay(10);
}
