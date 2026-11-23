// Kopieren nach include/secrets.h und ausfuellen. secrets.h kommt NICHT ins Git.
#pragma once
#define WLAN_NAME "Mein-WLAN"
#define WLAN_PASSWORT "geheim"
// ntfy-Thema = wie ein Passwort: lang und zufaellig, sonst kann jeder mitlesen.
// Handy: App "ntfy" installieren -> dieses Thema abonnieren.
#define NTFY_THEMA "hund-xxxxxxxxxxxxxxxx"
// Gemeinsamer Funk-Schluessel Halsband <-> Station (beliebiger langer Text, auf beiden gleich)
#define FUNK_SCHLUESSEL "bitte-aendern-langer-zufaelliger-text"

// ---- App unterwegs (Fernzugriff ueber MQTT, alles verschluesselt) ----
// Thema + Schluessel: lange Zufallstexte. Die App holt sie sich einmal im Heim-WLAN.
#define MQTT_THEMA "xxxxxxxxxxxxxxxxxxxx"
#define APP_SCHLUESSEL "bitte-aendern-noch-ein-langer-zufaelliger-text"
// Optional eigener Broker (z. B. HiveMQ Cloud, kostenlos mit Konto):
// #define MQTT_HOST "xxxx.s1.eu.hivemq.cloud"
// #define MQTT_BENUTZER "..."
// #define MQTT_PASSWORT "..."
