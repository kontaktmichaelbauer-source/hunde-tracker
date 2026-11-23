# Hundetracker (ROSAKuh)

GPS-Halsband mit Grenze: **Hund über die Grenze → sofort lauter Ton + starke Vibration am Halsband + Meldung aufs Handy.
Hund wieder drinnen → Ton und Vibration aus, Handy bekommt „wieder drinnen“.**

```
 Halsband (GPS + LoRa)  ──Funk 869 MHz──►  Station im Haus (WLAN)  ──Internet──►  Handy (App „ntfy“)
   Ton + Vibration                            Karte: http://hundetracker.local
```

## Teile (ca. 80–100 €)

| Teil | Wofür | ca. |
|---|---|---|
| Heltec **Wireless Tracker** V1.1 (ESP32-S3 + GPS + LoRa 868) | Halsband | 30 € |
| Heltec **WiFi LoRa 32 V3** (oder ein 2. Wireless Tracker) | Station im Haus | 20 € |
| LiPo 1S **3000 mAh** mit Schutzschaltung, JST 1,25 | Akku Halsband (3–4 Tage) | 10 € |
| **Piezo-Sirene 12 V, 100–110 dB** (kleine Alarmsirene) | lauter Ton | 5 € |
| Step-up **MT3608** (3,7 → 12 V) | Strom für die Sirene | 2 € |
| **Unwucht-Vibrationsmotor 3 V, zylindrisch** (7×25 mm o. ä., 80–150 mA) | starke Vibration (Münz-Motoren sind zu schwach durchs Fell) | 3 € |
| 2× Logik-MOSFET **AO3400** (oder IRLZ44N), 2× 100 kΩ, 1× Schottky 1N5819 | Ton/Vibration schalten | 2 € |
| Wasserdichtes Gehäuse (PETG/TPU drucken oder IP67-Box), breites Halsband | | 10 € |

Bei 60 kg Hund sind 200–300 g am Halsband kein Problem.

## Verdrahtung Halsband

```
GPIO5 ──100Ω──► Gate MOSFET1 (100k Gate→GND)   Drain → Sirene(−)   Sirene(+) → MT3608 OUT (12 V)
                                               MT3608 IN ← Akku (+), GND gemeinsam
GPIO6 ──100Ω──► Gate MOSFET2 (100k Gate→GND)   Drain → Motor(−)    Motor(+) → Akku (+)
                                               Diode 1N5819 parallel zum Motor (Strich an +)
Akku → Akku-Buchse des Wireless Tracker.  GPS-Antenne oben am Halsband (Richtung Himmel).
Taste „USER“ (GPIO0): 2 s halten = Spaziergang-Pause 60 min an/aus (2 Piepser = an, 1 langer = aus).
```

Station: nur USB-Netzteil. Optional ein Summer an GPIO5 (über MOSFET) – piept im Haus, solange der Hund draußen ist.

## Einrichten

1. **PlatformIO** (einmalig): `python3 -m venv ~/.platformio-venv && ~/.platformio-venv/bin/pip install platformio`
2. `cp include/secrets.example.h include/secrets.h` und ausfüllen:
   WLAN, **ntfy-Thema** (lang + zufällig, z. B. `hund-` + 16 zufällige Zeichen), **Funk-Schlüssel** (beliebiger langer Text).
3. Handy: App **ntfy** installieren → „+“ → das Thema aus secrets.h abonnieren.
   In der App für das Thema „Dringend“-Töne erlauben (Meldung „HUND DRAUSSEN“ kommt mit höchster Priorität).
4. Flashen (Board per USB-C anstecken):
   ```
   ~/.platformio-venv/bin/pio run -e station  -t upload
   ~/.platformio-venv/bin/pio run -e halsband -t upload
   ```
5. Im Heim-WLAN **http://hundetracker.local** öffnen (Android: IP aus der ntfy-Startmeldung „Station an“).
6. **Grenze bearbeiten** → Ecken auf die Satellitenkarte tippen → Speichern.
   Tipp: Halsband zu einer Ecke tragen und **„Punkt = Halsband-Position“** – dann stimmt die Grenze mit dem überein, was das GPS dort misst.
   Die Grenze wird beim nächsten Funkkontakt (≤ 10 s) aufs Halsband übertragen und dort gespeichert.
7. **Ton testen** drücken → Halsband piept/vibriert 3 s.

## App (zu Hause und unterwegs)

Eine Web-App für iPhone und Android, zwei Wege zur selben Oberfläche:

- **Zu Hause:** `http://hundetracker.local` – direkt an der Station, geht auch ohne Internet.
- **Unterwegs:** `https://kontaktmichaelbauer-source.github.io/hunde-tracker/` – Live-Karte, Spur, Grenze, Pause, Ton-Test.
  Die Station schickt jeden Stand **verschlüsselt** (AES-256) über einen MQTT-Broker (Standard: öffentlicher `broker.hivemq.com`).
  Der Broker sieht nur Zufallsdaten; ohne `APP_SCHLUESSEL` kann niemand mitlesen oder Befehle schicken.

**Einrichten (einmal):** Zu Hause im WLAN `http://hundetracker.local` öffnen → **„Unterwegs-App einrichten“** →
die Unterwegs-App öffnet sich mit Schlüssel → **zum Home-Bildschirm hinzufügen**
(iPhone: Teilen → „Zum Home-Bildschirm“, Android: ⋮ → „App installieren“).
Fragt die App nach Thema/Schlüssel (iPhone merkt sich das im Home-Bildschirm-Modus extra), die Werte aus `secrets.h` eintragen.

Alarm-Meldungen kommen weiter über **ntfy** (läuft auch, wenn die App zu ist).
Zuverlässiger als der öffentliche Broker: kostenloses Konto bei HiveMQ Cloud, Zugangsdaten in `secrets.h` (`MQTT_HOST/BENUTZER/PASSWORT`).

## Wie es entscheidet

- GPS jede Sekunde. Nur gute Messungen zählen (≥ 5 Satelliten, HDOP ≤ 4).
- **Alarm an:** 2 Messungen hintereinander mehr als **3 m** außerhalb der Linie (ca. 2 s).
- **Alarm aus:** 2 Messungen hintereinander mehr als **1 m** wieder drinnen.
- Einzelne GPS-Sprünge lösen nichts aus. Fällt GPS draußen aus, läuft der Alarm bis zu 5 min weiter.
- Ton + Vibration entscheidet das **Halsband selbst** – funktioniert auch, wenn Station/WLAN aus sind.
- Handy: sofort „HUND DRAUSSEN“ (mit Karten-Link), jede Minute „Immer noch draußen“, „wieder drinnen“,
  „Kein Signal vom Halsband“ nach 2 min, „Akku schwach“ unter 3,55 V.
- **Spaziergang:** Knopf auf der Webseite oder Taste am Halsband → 60 min kein Alarm.

Werte ändern: `lib/grenze/grenze.h` (`WaechterCfg`), Takte/Ton-Muster in `src/halsband/main.cpp`.

## Testen ohne Hardware

```
g++ -std=c++17 -I lib/grenze test/test_grenze.cpp -o /tmp/tg && /tmp/tg   # Grenz-Logik
python3 test/mock_station.py     # Webseite mit simuliertem Hund: http://127.0.0.1:8791
~/.platformio-venv/bin/pio run   # beide Firmwares bauen
```

## Noch offen / an der Hardware prüfen

- Pins des Wireless Tracker V1.1 (GPS 33/34, Vext 3, Akku 1/2) beim ersten Start im seriellen Monitor prüfen (`pio device monitor`).
- Akku-Faktor 4,9 (Spannungsteiler) mit Multimeter vergleichen.
- Funk-Reichweite über das ganze Gelände ablaufen (Webseite zeigt „Funk … dBm“; unter −115 wird es knapp).
- Funk ist mit Prüfsumme gesichert, aber nicht verschlüsselt (Position könnte mitgehört werden).
