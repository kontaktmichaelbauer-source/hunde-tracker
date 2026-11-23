#!/usr/bin/env python3
"""Nachgebaute Station zum Testen der Webseite am Mac (ohne Hardware).
python3 test/mock_station.py  ->  http://127.0.0.1:8791
Der Hund laeuft im Kreis und ueberquert dabei die Grenze."""
import json, math, os, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HIER = os.path.dirname(os.path.abspath(__file__))
SEITE = os.path.join(HIER, '..', 'web', 'index.html')
LA0, LO0 = 48.1, 11.5
KX = math.cos(math.radians(LA0)) * 111320

def m2ll(x, y): return [LA0 + y / 110540, LO0 + x / KX]

grenze = {'version': 1, 'punkte': [m2ll(0, 0), m2ll(170, 0), m2ll(170, 170), m2ll(0, 170)]}
spur, pause, start = [], False, time.time()

def drin(lat, lon, pk):
    x, y = (lon - LO0) * KX, (lat - LA0) * 110540
    c = False
    for i in range(len(pk)):
        a, b = pk[i], pk[i - 1]
        xi, yi = (a[1] - LO0) * KX, (a[0] - LA0) * 110540
        xj, yj = (b[1] - LO0) * KX, (b[0] - LA0) * 110540
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi: c = not c
    return c

def status():
    t = time.time() - start
    w = t / 60 * 2 * math.pi        # 1 Runde pro Minute
    lat, lon = m2ll(85 + 100 * math.cos(w), 85 + 60 * math.sin(w))
    draussen = len(grenze['punkte']) >= 3 and not drin(lat, lon, grenze['punkte'])
    if not spur or spur[-1] != [lat, lon]: spur.append([lat, lon])
    del spur[:-300]
    return {'da': True, 'alter_s': 1, 'fix': True, 'lat': lat, 'lon': lon,
            'zustand': 'draussen' if draussen else 'drinnen', 'abstand': 12.0 if draussen else -20.0,
            'alarm': draussen and not pause, 'pause': pause, 'pauseMin': 60 if pause else 0,
            'akku_mv': 3950, 'sats': 11, 'hdop': 0.9, 'rssi': -71, 'snr': 9.5,
            'halsbandGrenze': grenze['version'], 'befehlWartet': 0, 'grenze': grenze, 'spur': spur}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def antwort(self, code, body, typ='application/json'):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code); self.send_header('Content-Type', typ)
        self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path.split('?')[0] == '/': self.antwort(200, open(SEITE, 'rb').read(), 'text/html; charset=utf-8')
        elif self.path.startswith('/api/status'): self.antwort(200, json.dumps(status()))
        elif self.path.lstrip('/') in ('manifest.json', 'icon-192.png', 'icon-512.png', 'apple-touch-icon.png'):
            n = self.path.lstrip('/')
            typ = 'application/manifest+json' if n.endswith('.json') else 'image/png'
            self.antwort(200, open(os.path.join(HIER, '..', 'web', n), 'rb').read(), typ)
        else: self.antwort(404, 'nix')
    def do_POST(self):
        global pause
        d = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))) or '{}')
        if self.path == '/api/grenze':
            grenze['punkte'] = d['punkte']; grenze['version'] += 1
        elif self.path == '/api/befehl':
            if d.get('befehl') == 'pause': pause = True
            if d.get('befehl') == 'pause_aus': pause = False
        print(self.path, json.dumps(d))
        self.antwort(200, '{"ok":true}')

ThreadingHTTPServer(('127.0.0.1', int(os.environ.get('PORT', 8791))), H).serve_forever()
