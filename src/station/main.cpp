// STATION – Heltec Wireless Tracker (oder WiFi LoRa 32 V3) im Haus, am WLAN.
// Empfaengt das Halsband per LoRa, schickt Meldungen aufs Handy (ntfy) und zeigt
// unter http://hundetracker.local die Karte: Grenze einzeichnen, Hund live sehen.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "grenze.h"
#include "funk.h"
#include "secrets.h"
#include "fern.h"

#define VEXT_CTRL 3
#define TFT_LED 21
#define LED_PIN 18
#define SUMMER_PIN 5   // optional: Summer im Haus (aktiv, ueber MOSFET)

static const uint32_t KEIN_SIGNAL_MS = 120000;   // so lange nichts gehoert -> Meldung
static const uint32_t ALARM_WIEDERHOLEN_MS = 60000;
static const uint16_t AKKU_WARN_MV = 3550;

extern const char seite_start[] asm("_binary_web_index_html_start");
extern const char manifest_start[] asm("_binary_web_manifest_json_start");
extern const uint8_t ic192_s[] asm("_binary_web_icon_192_png_start");
extern const uint8_t ic192_e[] asm("_binary_web_icon_192_png_end");
extern const uint8_t ic512_s[] asm("_binary_web_icon_512_png_start");
extern const uint8_t ic512_e[] asm("_binary_web_icon_512_png_end");
extern const uint8_t apple_s[] asm("_binary_web_apple_touch_icon_png_start");
extern const uint8_t apple_e[] asm("_binary_web_apple_touch_icon_png_end");

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
WebServer web(80);
Preferences prefs;
Grenze grenze;

Position letzte{};
bool schonGehoert = false;
uint32_t empfangenUm = 0;
float rssi = 0, snr = 0;
uint8_t befehl = B_NICHTS;
uint16_t befehlParam = 0;

// Spur (letzte Positionen)
static const int SPUR_MAX = 300;
int32_t spurLat[SPUR_MAX], spurLon[SPUR_MAX];
int spurN = 0, spurKopf = 0;

// Meldungs-Zustand
bool gemeldetDraussen = false, gemeldetWeg = false;
uint32_t letzteAlarmMeldung = 0, letzteAkkuMeldung = 0;

// ---------- Handy-Meldung ueber ntfy ----------
void melden(const char* titel, const String& text, int prio, const char* tags, bool karte) {
  if (WiFi.status() != WL_CONNECTED) { Serial.println("Meldung verworfen: kein WLAN"); return; }
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(tls, String("https://ntfy.sh/") + NTFY_THEMA)) return;
  http.addHeader("Title", titel);
  http.addHeader("Priority", String(prio));
  http.addHeader("Tags", tags);
  if (karte && (letzte.flags & F_FIX)) {
    char url[96];
    snprintf(url, sizeof(url), "https://maps.google.com/?q=%.6f,%.6f", letzte.lat * 1e-7, letzte.lon * 1e-7);
    http.addHeader("Click", url);
  }
  int code = http.POST(text);
  Serial.printf("ntfy %s -> %d\n", titel, code);
  http.end();
}

// ---------- Grenze ----------
void grenzeLaden() {
  prefs.begin("hund", true);
  if (prefs.getBytesLength("grenze") == sizeof(Grenze)) prefs.getBytes("grenze", &grenze, sizeof(Grenze));
  prefs.end();
}
void grenzeSpeichern() {
  prefs.begin("hund", false);
  prefs.putBytes("grenze", &grenze, sizeof(Grenze));
  prefs.end();
}

void fernStatus();

// ---------- Funk: Position empfangen, antworten ----------
void antworten(const Position& p) {
  Antwort a{};
  a.k.typ = P_ANTWORT;
  a.k.seq = p.k.seq;
  a.befehl = befehl;
  a.param = befehlParam;
  a.grenzVersion = grenze.version;
  size_t len = offsetof(Antwort, pkt);
  if (p.grenzVersion != grenze.version) {          // Halsband hat alte Grenze -> mitschicken
    a.n = grenze.n;
    for (int i = 0; i < grenze.n; i++) { a.pkt[2 * i] = grenze.lat[i]; a.pkt[2 * i + 1] = grenze.lon[i]; }
    len += grenze.n * 8;
  }
  uint8_t buf[sizeof(Antwort) + MAC_LEN];
  memcpy(buf, &a, len);
  size_t n = funkSiegeln(FUNK_SCHLUESSEL, buf, len);
  delay(15);   // Halsband auf Empfang umschalten lassen
  radio.transmit(buf, n);
  if (befehl == B_TEST) befehl = B_NICHTS;          // Pause/Pause-aus bleiben, bis das Halsband es meldet
}

void positionVerarbeiten(const Position& p) {
  letzte = p;
  schonGehoert = true;
  empfangenUm = millis();
  rssi = radio.getRSSI();
  snr = radio.getSNR();

  if (befehl == B_PAUSE && (p.flags & F_PAUSE)) befehl = B_NICHTS;
  if (befehl == B_PAUSE_AUS && !(p.flags & F_PAUSE)) befehl = B_NICHTS;

  if (p.flags & F_FIX) {
    int vor = (spurKopf - 1 + SPUR_MAX) % SPUR_MAX;
    if (!spurN || spurLat[vor] != p.lat || spurLon[vor] != p.lon) {
      spurLat[spurKopf] = p.lat; spurLon[spurKopf] = p.lon;
      spurKopf = (spurKopf + 1) % SPUR_MAX;
      if (spurN < SPUR_MAX) spurN++;
    }
  }

  antworten(p);   // zuerst quittieren, dann (langsam) aufs Handy melden
  fernStatus();   // Stand an die App unterwegs

  const bool draussen = p.zustand == DRAUSSEN && !(p.flags & F_PAUSE);
  digitalWrite(SUMMER_PIN, draussen);
  digitalWrite(LED_PIN, draussen);
  char txt[96];
  if (draussen && (!gemeldetDraussen || millis() - letzteAlarmMeldung > ALARM_WIEDERHOLEN_MS)) {
    snprintf(txt, sizeof(txt), "%s - %.0f m ausserhalb. Tippen = Karte.",
             gemeldetDraussen ? "Immer noch draussen" : "Hund ist ueber die Grenze!", p.abstand_dm / 10.0);
    melden("HUND DRAUSSEN", txt, 5, "rotating_light,dog", true);
    gemeldetDraussen = true;
    letzteAlarmMeldung = millis();
  } else if (!draussen && gemeldetDraussen) {
    melden("Hund wieder drinnen", "Alarm aus, er ist zurueck.", 3, "white_check_mark,dog", false);
    gemeldetDraussen = false;
  }
  if (gemeldetWeg) {
    melden("Halsband wieder da", "Funk zum Halsband steht wieder.", 3, "satellite", false);
    gemeldetWeg = false;
  }
  if (p.akku_mv && p.akku_mv < AKKU_WARN_MV && (!letzteAkkuMeldung || millis() - letzteAkkuMeldung > 6UL * 3600000UL)) {
    snprintf(txt, sizeof(txt), "Akku Halsband %.2f V - bald laden.", p.akku_mv / 1000.0);
    melden("Akku schwach", txt, 4, "battery", false);
    letzteAkkuMeldung = millis();
  }
}

void funkPruefenUndLesen() {
  if (!funkFlag) return;
  funkFlag = false;
  uint8_t rx[64];
  size_t len = radio.getPacketLength();
  if (len > 0 && len <= sizeof(rx) && radio.readData(rx, len) == RADIOLIB_ERR_NONE) {
    size_t nutz = funkPruefen(FUNK_SCHLUESSEL, rx, len);
    if (nutz >= sizeof(Position) && rx[2] == P_POSITION) {
      Position p;
      memcpy(&p, rx, sizeof(p));
      positionVerarbeiten(p);
    } else {
      Serial.printf("Paket verworfen (%u Byte)\n", (unsigned)len);
    }
  }
  radio.startReceive();
}

// ---------- Stand als JSON (lokal und unterwegs gleich) ----------
void statusJson(JsonDocument& d) {
  d["da"] = schonGehoert;
  d["alter_s"] = schonGehoert ? (millis() - empfangenUm) / 1000 : -1;
  d["t"] = (uint32_t)time(nullptr);
  d["fix"] = (bool)(letzte.flags & F_FIX);
  d["lat"] = letzte.lat * 1e-7;
  d["lon"] = letzte.lon * 1e-7;
  d["zustand"] = letzte.zustand == DRAUSSEN ? "draussen" : "drinnen";
  d["abstand"] = letzte.abstand_dm / 10.0;
  d["alarm"] = (bool)(letzte.flags & F_ALARM);
  d["pause"] = (bool)(letzte.flags & F_PAUSE);
  d["pauseMin"] = letzte.pauseMin;
  d["akku_mv"] = letzte.akku_mv;
  d["sats"] = letzte.sats;
  d["hdop"] = letzte.hdop10 / 10.0;
  d["rssi"] = rssi;
  d["snr"] = snr;
  d["halsbandGrenze"] = letzte.grenzVersion;
  d["befehlWartet"] = befehl;
}
void karteJson(JsonDocument& d) {
  JsonObject g = d["grenze"].to<JsonObject>();
  g["version"] = grenze.version;
  JsonArray pk = g["punkte"].to<JsonArray>();
  for (int i = 0; i < grenze.n; i++) { JsonArray q = pk.add<JsonArray>(); q.add(grenze.lat[i] * 1e-7); q.add(grenze.lon[i] * 1e-7); }
  JsonArray sp = d["spur"].to<JsonArray>();
  for (int i = 0; i < spurN; i++) {
    int k = (spurKopf - spurN + i + SPUR_MAX) % SPUR_MAX;
    JsonArray q = sp.add<JsonArray>(); q.add(spurLat[k] * 1e-7); q.add(spurLon[k] * 1e-7);
  }
}

bool grenzeSetzen(JsonArrayConst pk, String& fehler) {
  if (pk.size() != 0 && (pk.size() < 3 || pk.size() > GRENZE_MAX_PUNKTE)) {
    fehler = "3 bis " + String(GRENZE_MAX_PUNKTE) + " Punkte"; return false;
  }
  grenze.n = pk.size();
  for (int i = 0; i < grenze.n; i++) {
    grenze.lat[i] = (int32_t)llround(pk[i][0].as<double>() * 1e7);
    grenze.lon[i] = (int32_t)llround(pk[i][1].as<double>() * 1e7);
  }
  grenze.version++;
  if (grenze.version == 0 || grenze.version == 0xFFFF) grenze.version = 1;
  grenzeSpeichern();
  return true;
}

bool befehlSetzen(const String& b, int min) {
  if (b == "test") befehl = B_TEST;
  else if (b == "pause") { befehl = B_PAUSE; befehlParam = min > 0 ? min : 60; }
  else if (b == "pause_aus") befehl = B_PAUSE_AUS;
  else return false;
  return true;
}

// ---------- Fernzugriff (App unterwegs) ----------
WiFiClientSecure mqttTls;
PubSubClient mqtt(mqttTls);
uint32_t letzteKarte = 0, letzterVersuch = 0, letzterBefehlT = 0;

void fernSenden(const char* teil, JsonDocument& d) {
  if (!mqtt.connected()) return;
  String s; serializeJson(d, s);
  String v = fernVerpacken(s);
  mqtt.publish(fernThema(teil).c_str(), (const uint8_t*)v.c_str(), v.length(), true);
}
void fernStatus() { JsonDocument d; statusJson(d); fernSenden("status", d); }
void fernKarte() { JsonDocument d; karteJson(d); fernSenden("karte", d); letzteKarte = millis(); }

void fernNachricht(char* thema, uint8_t* daten, unsigned int len) {
  String klar;
  if (!fernAuspacken(daten, len, klar)) { Serial.println("Fern: falscher Schluessel/kaputt"); return; }
  JsonDocument d;
  if (deserializeJson(d, klar)) return;
  const uint32_t t = d["t"] | 0, jetzt = time(nullptr);
  // Schutz gegen abgefangene + spaeter wiederholte Befehle
  if (jetzt < 1700000000UL || t <= letzterBefehlT || t + 300 < jetzt || t > jetzt + 300) {
    Serial.println("Fern: Befehl zu alt / Uhr nicht gestellt"); return;
  }
  letzterBefehlT = t;
  String b = d["befehl"] | "";
  if (b == "grenze") { String f; if (grenzeSetzen(d["punkte"].as<JsonArrayConst>(), f)) fernKarte(); }
  else befehlSetzen(b, d["min"] | 60);
  fernStatus();
}

void fernPflegen() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (letzterVersuch && millis() - letzterVersuch < 10000) return;
    letzterVersuch = millis();
    String id = "hundest-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    bool ok = strlen(MQTT_BENUTZER) ? mqtt.connect(id.c_str(), MQTT_BENUTZER, MQTT_PASSWORT) : mqtt.connect(id.c_str());
    Serial.printf("MQTT %s\n", ok ? "verbunden" : "Fehler");
    if (ok) { mqtt.subscribe(fernThema("befehl").c_str()); fernKarte(); fernStatus(); }
    return;
  }
  mqtt.loop();
  if (millis() - letzteKarte > 60000) fernKarte();
}

// ---------- Webseite + API (im Heim-WLAN) ----------
void apiStatus() {
  JsonDocument d; statusJson(d); karteJson(d);
  String s; serializeJson(d, s);
  web.send(200, "application/json", s);
}

void apiGrenze() {
  JsonDocument d;
  if (deserializeJson(d, web.arg("plain"))) { web.send(400, "text/plain", "JSON kaputt"); return; }
  String f;
  if (!grenzeSetzen(d["punkte"].as<JsonArrayConst>(), f)) { web.send(400, "text/plain", f); return; }
  fernKarte();
  web.send(200, "application/json", "{\"ok\":true}");
}

void apiBefehl() {
  JsonDocument d;
  if (deserializeJson(d, web.arg("plain"))) { web.send(400, "text/plain", "JSON kaputt"); return; }
  if (!befehlSetzen(d["befehl"] | "", d["min"] | 60)) { web.send(400, "text/plain", "unbekannt"); return; }
  web.send(200, "application/json", "{\"ok\":true}");
}

// Zugangsdaten fuer die App unterwegs (nur im Heim-WLAN abrufbar)
void apiFern() {
  JsonDocument d;
  d["thema"] = MQTT_THEMA;
  d["schluessel"] = APP_SCHLUESSEL;
  d["host"] = MQTT_HOST;
  String s; serializeJson(d, s);
  web.send(200, "application/json", s);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT); pinMode(SUMMER_PIN, OUTPUT);
  pinMode(VEXT_CTRL, OUTPUT); digitalWrite(VEXT_CTRL, LOW);   // GPS/Display brauchen wir hier nicht
  pinMode(TFT_LED, OUTPUT); digitalWrite(TFT_LED, LOW);
  grenzeLaden();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("hundetracker");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WLAN_NAME, WLAN_PASSWORT);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  Serial.printf("WLAN %s  IP %s\n", WiFi.status() == WL_CONNECTED ? "ok" : "FEHLT", WiFi.localIP().toString().c_str());
  MDNS.begin("hundetracker");
  configTime(0, 0, "pool.ntp.org", "time.google.com");   // Uhr fuer Befehls-Zeitstempel

  web.on("/", HTTP_GET, [] { web.send(200, "text/html; charset=utf-8", seite_start); });
  web.on("/manifest.json", HTTP_GET, [] { web.send(200, "application/manifest+json", manifest_start); });
  web.on("/icon-192.png", HTTP_GET, [] { web.send_P(200, "image/png", (const char*)ic192_s, ic192_e - ic192_s); });
  web.on("/icon-512.png", HTTP_GET, [] { web.send_P(200, "image/png", (const char*)ic512_s, ic512_e - ic512_s); });
  web.on("/apple-touch-icon.png", HTTP_GET, [] { web.send_P(200, "image/png", (const char*)apple_s, apple_e - apple_s); });
  web.on("/api/status", HTTP_GET, apiStatus);
  web.on("/api/grenze", HTTP_POST, apiGrenze);
  web.on("/api/befehl", HTTP_POST, apiBefehl);
  web.on("/api/fern", HTTP_GET, apiFern);
  web.begin();
  MDNS.addService("http", "tcp", 80);

  fernInit();
  mqttTls.setInsecure();
  mqttTls.setTimeout(5);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(16384);
  mqtt.setKeepAlive(30);
  mqtt.setCallback(fernNachricht);

  if (funkStart(radio)) radio.startReceive();
  melden("Hundetracker Station an", "Station laeuft. IP " + WiFi.localIP().toString(), 2, "house", false);
}

void loop() {
  funkPruefenUndLesen();
  web.handleClient();
  fernPflegen();
  if (schonGehoert && !gemeldetWeg && millis() - empfangenUm > KEIN_SIGNAL_MS) {
    melden("Kein Signal vom Halsband", "Seit 2 Minuten nichts gehoert (Akku leer, ausser Reichweite?).", 4, "warning", true);
    gemeldetWeg = true;
    fernStatus();
  }
  delay(2);
}
