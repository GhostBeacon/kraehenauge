#include "config.h"

#include <SD.h>
#include <SPI.h>

// SD-Pins des Cardputer ADV
static constexpr int SD_SCK = 40;
static constexpr int SD_MISO = 39;
static constexpr int SD_MOSI = 14;
static constexpr int SD_CS = 12;

static constexpr int MAX_WIFIS = 9;

static const char* CONFIG_PATH = "/raveneye/config.txt";

// ntfy-Topics: [-_A-Za-z0-9]{1,64}, mehrere mit Komma
static bool validTopics(const String& topics) {
    if (topics.isEmpty()) return false;
    int len = 0;
    for (char c : topics) {
        if (c == ',') {
            if (len == 0) return false;
            len = 0;
        } else if (isalnum((unsigned char)c) || c == '-' || c == '_') {
            if (++len > 64) return false;
        } else {
            return false;
        }
    }
    return len > 0;
}

// "wifi_ssid" -> 1, "wifi2_ssid" -> 2 ... ; 0 = kein WLAN-Schluessel. field = "ssid" / "pass"
static int wifiIndex(const String& key, const char* field) {
    String suffix = String("_") + field;
    if (!key.startsWith("wifi") || !key.endsWith(suffix)) return 0;
    String mid = key.substring(4, key.length() - suffix.length());
    if (mid.isEmpty()) return 1;
    if (mid.length() == 1 && mid[0] >= '2' && mid[0] <= '0' + MAX_WIFIS) return mid[0] - '0';
    return 0;
}

bool loadConfig(Config& cfg, String& error) {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 25000000)) {
        error = "Keine SD-Karte";
        return false;
    }
    File f = SD.open(CONFIG_PATH);
    if (!f) {
        error = String(CONFIG_PATH) + " fehlt";
        return false;
    }
    WifiNetwork nets[MAX_WIFIS];
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line[0] == '#') continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        key.trim();
        val.trim();
        if (int i = wifiIndex(key, "ssid")) nets[i - 1].ssid = val;
        else if (int i = wifiIndex(key, "pass")) nets[i - 1].pass = val;
        else if (key == "ntfy_server") cfg.ntfyServer = val;
        else if (key == "ntfy_topics") cfg.ntfyTopics = val;
        else if (key == "status_url") cfg.statusUrl = val;
        else if (key == "status_slug") cfg.statusSlug = val;
        else if (key == "server_url") cfg.serverUrl = val;
        else if (key == "meldungen") cfg.meldungen = val;
    }
    f.close();
    SD.end();  // Karte wird danach nicht mehr gebraucht

    for (auto& n : nets)
        if (!n.ssid.isEmpty()) cfg.wifis.push_back(n);

    if (cfg.wifis.empty()) {
        error = "wifi_ssid fehlt";
        return false;
    }
    for (String* url : {&cfg.statusUrl, &cfg.serverUrl}) {
        while (url->endsWith("/")) url->remove(url->length() - 1);
        if (!url->isEmpty() && !url->startsWith("https://")) {
            error = "URL muss https:// sein";
            return false;
        }
    }
    cfg.meldungen.toLowerCase();
    if (!cfg.meldungen.isEmpty() && cfg.meldungen != "ntfy" && cfg.meldungen != "meldestelle") {
        error = "meldungen= ntfy oder meldestelle";
        return false;
    }
    if (cfg.meldungen == "meldestelle" && cfg.serverUrl.isEmpty()) {
        error = "meldungen=meldestelle braucht server_url";
        return false;
    }
    if (cfg.nutztNtfy() && !validTopics(cfg.ntfyTopics)) {   // ntfy_topics nur noetig, wenn ntfy genutzt wird
        error = "ntfy_topics ungueltig";
        return false;
    }
    return true;
}
