// Konfiguration von der SD-Karte: /raveneye/config.txt (key=value, # = Kommentar).
// Zugangsdaten liegen nur auf der Karte, nie in der Firmware oder im Repo.
#pragma once

#include <Arduino.h>

#include <vector>

struct WifiNetwork {
    String ssid;
    String pass;
};

struct Config {
    std::vector<WifiNetwork> wifis;  // wifi_ssid/wifi_pass, wifi2_ssid/wifi2_pass ... wifi9_*
    String ntfyServer = "ntfy.sh";  // nur Hostname, immer HTTPS
    String ntfyTopics;              // kommagetrennt, z. B. "alarm,info"
    String statusUrl;               // Uptime Kuma, z. B. https://status.example.org (optional)
    String statusSlug;              // Slug der Statusseite
    String serverUrl;               // Server mit /api/v1/system_stats, z. B. https://srv.example.org (optional)
    String meldungen;               // Quelle: "meldestelle" (Standard mit server_url) oder "ntfy"

    bool nutztNtfy() const { return meldungen == "ntfy" || (meldungen.isEmpty() && serverUrl.isEmpty()); }
};

// Liefert false und eine Fehlermeldung, wenn Karte/Datei fehlt oder Pflichtwerte leer sind.
bool loadConfig(Config& cfg, String& error);
