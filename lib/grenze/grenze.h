// Grenze (Geofence) + Waechter (Zustand drinnen/draussen mit Puffer).
// Reines C++, laeuft auf dem ESP32 und zum Testen am Mac (test/test_grenze.cpp).
#pragma once
#include <stdint.h>
#include <math.h>

#ifndef GRENZE_MAX_PUNKTE
#define GRENZE_MAX_PUNKTE 24   // passt in ein LoRa-Paket (24 x 8 Byte)
#endif

struct Grenze {
  uint16_t version = 0;           // steigt bei jeder Aenderung (Station -> Halsband)
  uint8_t n = 0;                  // Anzahl Eckpunkte (0 = keine Grenze, kein Alarm)
  int32_t lat[GRENZE_MAX_PUNKTE]; // Grad * 1e7
  int32_t lon[GRENZE_MAX_PUNKTE];
};

// Abstand zur Grenze in Metern: > 0 = draussen, < 0 = drinnen.
// Lokale Ebene um den ersten Punkt (fuer ein paar km genau genug).
inline float grenzAbstand(const Grenze& g, double lat, double lon) {
  if (g.n < 3) return -1e6f;
  const double la0 = g.lat[0] * 1e-7, lo0 = g.lon[0] * 1e-7;
  const double kx = cos(la0 * M_PI / 180.0) * 111320.0, ky = 110540.0;
  const double px = (lon - lo0) * kx, py = (lat - la0) * ky;
  bool drin = false;
  double best = 1e18;
  for (int i = 0, j = g.n - 1; i < g.n; j = i++) {
    const double xi = (g.lon[i] * 1e-7 - lo0) * kx, yi = (g.lat[i] * 1e-7 - la0) * ky;
    const double xj = (g.lon[j] * 1e-7 - lo0) * kx, yj = (g.lat[j] * 1e-7 - la0) * ky;
    if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) drin = !drin;
    const double dx = xj - xi, dy = yj - yi, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((px - xi) * dx + (py - yi) * dy) / l2 : 0;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    const double ex = xi + t * dx - px, ey = yi + t * dy - py, d2 = ex * ex + ey * ey;
    if (d2 < best) best = d2;
  }
  const float d = (float)sqrt(best);
  return drin ? -d : d;
}

enum Zustand : uint8_t { DRINNEN = 0, DRAUSSEN = 1 };

struct WaechterCfg {
  float raus_m = 3.0f;     // so weit muss er ueber der Linie sein -> Alarm
  float rein_m = 1.0f;     // so weit muss er wieder drin sein -> Alarm aus
  uint8_t bestaetigen = 2; // so viele Messungen hintereinander (1 Hz -> ~2 s)
  uint32_t ohne_fix_ms = 300000; // ohne GPS so lange weiter Alarm, dann aus
};

struct Waechter {
  WaechterCfg cfg;
  Zustand zustand = DRINNEN;
  uint8_t zaehler = 0;
  uint32_t letzterFix = 0;

  // gueltig = brauchbare GPS-Messung, abstand = grenzAbstand(), jetzt = millis()
  // Rueckgabe true = Zustand hat gewechselt.
  bool messung(bool gueltig, float abstand, uint32_t jetzt) {
    if (!gueltig) return false;
    letzterFix = jetzt;
    bool wechsel = false;
    if (zustand == DRINNEN) {
      zaehler = (abstand > cfg.raus_m) ? zaehler + 1 : 0;
      if (zaehler >= cfg.bestaetigen) { zustand = DRAUSSEN; wechsel = true; zaehler = 0; }
    } else {
      zaehler = (abstand < -cfg.rein_m) ? zaehler + 1 : 0;
      if (zaehler >= cfg.bestaetigen) { zustand = DRINNEN; wechsel = true; zaehler = 0; }
    }
    return wechsel;
  }
  // Ton/Vibration jetzt an?
  bool alarm(uint32_t jetzt) const {
    return zustand == DRAUSSEN && (jetzt - letzterFix) < cfg.ohne_fix_ms;
  }
};
