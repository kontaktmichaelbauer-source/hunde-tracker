// Am Mac: g++ -std=c++17 -I lib/grenze test/test_grenze.cpp -o /tmp/tg && /tmp/tg
#include "grenze.h"
#include <stdio.h>
#include <stdlib.h>

static int fehler = 0;
#define PRUEF(c, txt) do { if (!(c)) { printf("FEHLER: %s\n", txt); fehler++; } else printf("ok: %s\n", txt); } while (0)

// Punkt um (dx,dy) Meter vom Ursprung verschoben
static void pos(double la0, double lo0, double dx, double dy, double& la, double& lo) {
  la = la0 + dy / 110540.0;
  lo = lo0 + dx / (cos(la0 * M_PI / 180.0) * 111320.0);
}

int main() {
  // Quadrat 170 x 170 m (~3 ha) bei 48,1 N / 11,5 O
  const double la0 = 48.1, lo0 = 11.5;
  Grenze g; g.n = 4;
  double ec[4][2] = {{0, 0}, {170, 0}, {170, 170}, {0, 170}};
  for (int i = 0; i < 4; i++) {
    double la, lo; pos(la0, lo0, ec[i][0], ec[i][1], la, lo);
    g.lat[i] = (int32_t)llround(la * 1e7); g.lon[i] = (int32_t)llround(lo * 1e7);
  }
  double la, lo;
  pos(la0, lo0, 85, 85, la, lo);   PRUEF(fabs(grenzAbstand(g, la, lo) + 85) < 0.5, "Mitte = 85 m drinnen");
  pos(la0, lo0, 175, 85, la, lo);  PRUEF(fabs(grenzAbstand(g, la, lo) - 5) < 0.3, "5 m rechts draussen");
  pos(la0, lo0, 85, -2, la, lo);   PRUEF(fabs(grenzAbstand(g, la, lo) - 2) < 0.3, "2 m unten draussen");
  pos(la0, lo0, 180, 180, la, lo); PRUEF(fabs(grenzAbstand(g, la, lo) - 14.14) < 0.3, "Ecke diagonal 14 m");
  Grenze leer; PRUEF(grenzAbstand(leer, la, lo) < 0, "ohne Grenze nie draussen");

  // Waechter: Puffer + Bestaetigung
  Waechter w; uint32_t t = 0;
  w.messung(true, 2.0f, t += 1000);  PRUEF(w.zustand == DRINNEN, "2 m draussen = noch kein Alarm (Puffer 3 m)");
  w.messung(true, 4.0f, t += 1000);  PRUEF(w.zustand == DRINNEN, "1 Messung 4 m = noch kein Alarm");
  bool wx = w.messung(true, 4.5f, t += 1000);
  PRUEF(wx && w.zustand == DRAUSSEN && w.alarm(t), "2. Messung -> Alarm an");
  w.messung(true, 0.5f, t += 1000);  PRUEF(w.zustand == DRAUSSEN, "auf der Linie = Alarm bleibt");
  w.messung(true, -2.0f, t += 1000); PRUEF(w.zustand == DRAUSSEN, "1x drinnen = Alarm bleibt");
  wx = w.messung(true, -2.0f, t += 1000);
  PRUEF(wx && w.zustand == DRINNEN && !w.alarm(t), "2x drinnen -> Alarm aus");
  // Ausreisser: ein einzelner Sprung loest nichts aus
  w.messung(true, 30.0f, t += 1000); w.messung(true, -20.0f, t += 1000);
  PRUEF(w.zustand == DRINNEN, "einzelner GPS-Sprung = kein Alarm");
  // Ohne Fix
  w.messung(true, 5, t += 1000); w.messung(true, 5, t += 1000);
  w.messung(false, 0, t += 1000);    PRUEF(w.alarm(t), "GPS kurz weg = Alarm bleibt");
  PRUEF(!w.alarm(t + 400000), "5 min ohne GPS = Alarm aus");

  printf(fehler ? "\n%d FEHLER\n" : "\nalles gruen\n", fehler);
  return fehler ? 1 : 0;
}
