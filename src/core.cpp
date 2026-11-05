#include "core.h"

#include <M5Cardputer.h>
#include <WiFi.h>
#include <time.h>

#include "meldestelle.h"
#include "text.h"

namespace core {

Config cfg;
Settings settings;
Preferences prefs;
static NtfyClient ntfyClient;
static MeldestelleClient meldestelle;
static Quelle* aktiv = &ntfyClient;
std::deque<NtfyMessage> messages;
bool ntfyVisible = false;
bool keepAwake = false;

static constexpr size_t MAX_MESSAGES = 30;
static constexpr size_t MAX_TEXT = 1000;   // ntfy erlaubt 4096 - 30 volle Meldungen waeren ~120 KB RAM
static constexpr size_t MAX_TITLE = 150;

// Kuerzt UTF-8-sicher auf hoechstens max Bytes
static void clip(String& s, size_t max) {
    if (s.length() <= max) return;
    size_t n = max - 3;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;  // nicht mitten in einem Zeichen schneiden
    s.remove(n);
    s += "...";
}
static const char* TZ_BERLIN = "CET-1CEST,M3.5.0,M10.5.0/3";

static bool dirty = true;
static bool dimmed = false;
static uint32_t lastActivity = 0;
static int lastMinute = -1;
static String toastText;
static uint32_t toastUntil = 0;

// ---------- WLAN: nicht blockierend (Scan im Hintergrund, staerkstes bekanntes Netz) ----------

enum class WifiState { Idle, Scanning, Connecting, Connected };
static WifiState wifiState = WifiState::Idle;
static uint32_t wifiNextTry = 0;
static uint32_t wifiConnectStart = 0;
static uint32_t wifiScanStart = 0;
static int wifiScanFails = 0;     // Scans in Folge fehlgeschlagen
static size_t wifiDirectNext = 0; // Rueckfall ohne Scan: welches Netz als naechstes
static constexpr int SCAN_FAILS_BEFORE_DIRECT = 3;
static constexpr uint16_t SCAN_MS_PER_CHANNEL = 300;  // Scan-Zeitlimit = 20 x dieser Wert

// Diagnose fuer Einstellungen -> WLAN: die letzten Ereignisse, neueste zuletzt
static std::deque<String> wifiEvents;
static volatile int wifiDiscReason = 0;  // aus dem WLAN-Task, in wifiTick() ausgewertet

// Die haeufigsten Trennungsgruende (esp_wifi_types.h), der Rest nur als Nummer
static const char* reasonText(int r) {
    switch (r) {
        case 2: case 202: return "Anmeldung abgelehnt";
        case 15: case 204: return "Handshake - Passwort falsch?";
        case 201: return "Netz nicht gefunden";
        case 203: return "Zuordnung abgelehnt (MAC-Filter?)";
        case 8: return "vom Geraet getrennt";
        case 200: return "Signal verloren";
        default: return "";
    }
}

static void wifiLog(const String& s) {
    String line = String(millis() / 1000) + " s: " + s;
    Serial.println("wlan " + line);
    wifiEvents.push_back(line);
    if (wifiEvents.size() > 6) wifiEvents.pop_front();
}

static void wifiConnect(size_t w, const String& why) {
    wifiLog("verbinde: " + cfg.wifis[w].ssid + " (" + why + ")");
    WiFi.begin(cfg.wifis[w].ssid.c_str(), cfg.wifis[w].pass.c_str());
    wifiState = WifiState::Connecting;
    wifiConnectStart = millis();
}

// Scan fehlgeschlagen: meist ist der Chip noch mit einem Verbindungsversuch beschaeftigt
// (z. B. vom Launcher uebrig). Abbrechen, kurz warten, neu versuchen.
static void wifiScanFailed(const String& what) {
    wifiScanFails++;
    wifiLog(what + " (" + String(wifiScanFails) + ". Mal)");
    WiFi.scanDelete();
    WiFi.disconnect();
    wifiState = WifiState::Idle;
    wifiNextTry = millis() + 2000;
}

static void wifiTick() {
    uint32_t now = millis();
    if (int reason = wifiDiscReason) {
        wifiDiscReason = 0;
        wifiLog("getrennt, Grund " + String(reason) + " " + reasonText(reason));
    }
    switch (wifiState) {
        case WifiState::Connected:
            if (!WiFi.isConnected()) {
                wifiState = WifiState::Idle;
                wifiNextTry = now;
                dirty = true;
            }
            break;

        case WifiState::Idle:
            if (WiFi.isConnected()) {
                wifiState = WifiState::Connected;
                wifiLog("verbunden: " + WiFi.SSID());
                dirty = true;
            } else if ((int32_t)(now - wifiNextTry) >= 0) {
                if (wifiScanFails >= SCAN_FAILS_BEFORE_DIRECT) {
                    // Rueckfall: ohne eigenen Scan der Reihe nach die eingetragenen Netze
                    wifiScanFails = 0;
                    wifiConnect(wifiDirectNext++ % cfg.wifis.size(), "ohne Scan");
                } else if (WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHANNEL) == WIFI_SCAN_FAILED) {
                    wifiScanFailed("Scan-Start abgelehnt");
                } else {
                    wifiState = WifiState::Scanning;
                    wifiScanStart = now;
                }
            }
            break;

        case WifiState::Scanning: {
            int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) break;
            if (n < 0) {
                wifiScanFailed("Scan fehlgeschlagen nach " + String((now - wifiScanStart) / 1000) + " s");
                break;
            }
            wifiScanFails = 0;
            int best = -1, bestRssi = -1000;
            String seen;  // fuer die Diagnose: die ersten gefundenen Netze
            for (int i = 0; i < n && i < 3; i++) seen += (i ? ", " : "") + WiFi.SSID(i) + " " + String(WiFi.RSSI(i));
            for (int i = 0; i < n; i++) {
                for (size_t w = 0; w < cfg.wifis.size(); w++) {
                    if (WiFi.SSID(i) == cfg.wifis[w].ssid && WiFi.RSSI(i) > bestRssi) {
                        best = w;
                        bestRssi = WiFi.RSSI(i);
                    }
                }
            }
            WiFi.scanDelete();
            if (n >= 0 && best < 0) wifiLog("kein bekanntes Netz unter " + String(n) + ": " + seen);
            if (best >= 0) {
                wifiConnect(best, String(bestRssi) + " dBm");
            } else {
                wifiState = WifiState::Idle;
                wifiNextTry = now + 10000;
            }
            break;
        }

        case WifiState::Connecting:
            if (WiFi.isConnected()) {
                wifiState = WifiState::Connected;
                wifiLog("verbunden, IP " + WiFi.localIP().toString());
                dirty = true;
            } else if (now - wifiConnectStart > 15000) {
                wifiLog("Zeitueberschreitung, Status " + String((int)WiFi.status()));
                WiFi.disconnect();
                wifiState = WifiState::Idle;
                wifiNextTry = now + 3000;
            }
            break;
    }
}

// ---------- Meldungen ----------

static void onMessage(NtfyMessage&& m) {
    for (auto& e : messages)
        if (e.id == m.id) return;
    m.title = text::sanitize(m.title);
    m.message = text::sanitize(m.message);
    clip(m.title, MAX_TITLE);
    clip(m.message, MAX_TEXT);

    // Beim Start nachgeladene Meldungen (aelter als 2 min) klingeln nicht
    bool fresh = timeValid() && time(nullptr) - (time_t)m.time < 120;
    if (fresh) {
        wake();
        beep(m.priority);
        if (!ntfyVisible) toast("Neu: " + (m.title.isEmpty() ? m.message : m.title));
    } else {
        m.seen = true;
    }
    messages.push_front(std::move(m));
    if (messages.size() > MAX_MESSAGES) messages.pop_back();
    dirty = true;
}

// ---------- oeffentlich ----------

void begin() {
    prefs.begin("raveneye", false);
    settings.muted = prefs.getBool("muted", false);
    settings.brightness = prefs.getUChar("bright", 128);
    settings.dimAfterMs = prefs.getULong("dim", 60000);
    applyBrightness();
    M5Cardputer.Speaker.setVolume(96);

    WiFi.persistent(false);  // Zugangsdaten nicht in den (mit dem Launcher geteilten) NVS schreiben
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();       // einen vom Launcher uebrigen Verbindungsversuch abbrechen
    WiFi.setAutoReconnect(false);  // wifiTick() waehlt selbst das staerkste Netz
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { wifiDiscReason = info.wifi_sta_disconnected.reason; },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    configTzTime(TZ_BERLIN, "pool.ntp.org", "time.cloudflare.com");
    if (cfg.nutztNtfy()) {
        ntfyClient.begin(cfg.ntfyServer, cfg.ntfyTopics, onMessage);
        aktiv = &ntfyClient;
    } else {
        meldestelle.begin(cfg.serverUrl, onMessage);
        aktiv = &meldestelle;
    }
    lastActivity = millis();
}

void tick() {
    wifiTick();
    // TLS braucht eine gueltige Uhrzeit fuer die Zertifikatspruefung
    static Quelle::State lastState = Quelle::State::Idle;
    aktiv->poll(online() && timeValid());
    if (aktiv->state() != lastState) {
        lastState = aktiv->state();
        dirty = true;
    }

    if (timeValid()) {
        int minute = time(nullptr) / 60;
        if (minute != lastMinute) {
            lastMinute = minute;
            dirty = true;
        }
    }
    if (toastUntil && (int32_t)(millis() - toastUntil) >= 0) {
        toastUntil = 0;
        toastText = "";
        dirty = true;
    }
    // Speicher beobachten: seriell alle 30 s, Warnung einmalig, wenn es eng wird
    static uint32_t lastHeapLog = 0;
    static bool warned = false;
    if (millis() - lastHeapLog > 30000) {
        lastHeapLog = millis();
        Serial.printf("heap frei %u, min %u, groesster Block %u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                      ESP.getMaxAllocHeap());
        if (!warned && ESP.getMaxAllocHeap() < 20000) {
            warned = true;
            toast("Warnung: Arbeitsspeicher knapp", 8000);
        }
    }

    if (keepAwake) lastActivity = millis();
    if (!dimmed && settings.dimAfterMs && millis() - lastActivity > settings.dimAfterMs) {
        M5Cardputer.Display.setBrightness(max(4, settings.brightness / 8));
        dimmed = true;
    }
}

void saveSettings() {
    prefs.putBool("muted", settings.muted);
    prefs.putUChar("bright", settings.brightness);
    prefs.putULong("dim", settings.dimAfterMs);
}

void applyBrightness() {
    M5Cardputer.Display.setBrightness(settings.brightness);
    dimmed = false;
}

bool timeValid() { return time(nullptr) > 1700000000; }
bool online() { return WiFi.isConnected(); }
String wifiSsid() { return WiFi.isConnected() ? WiFi.SSID() : String(); }
int wifiRssi() { return WiFi.isConnected() ? WiFi.RSSI() : 0; }

String wifiDiagnosis() {
    String s;
    for (auto& e : wifiEvents) s += (s.isEmpty() ? "" : "\n") + e;
    return s.isEmpty() ? String("noch keine Ereignisse") : s;
}

Quelle& quelle() { return *aktiv; }
bool live() { return aktiv->state() == Quelle::State::Streaming; }

int unread() {
    int n = 0;
    for (auto& m : messages) n += !m.seen;
    return n;
}

bool wake() {
    lastActivity = millis();
    if (!dimmed) return false;
    applyBrightness();
    dirty = true;
    return true;
}

void beep(uint8_t priority) {
    if (settings.muted) return;
    M5Cardputer.Speaker.tone(priority >= 4 ? 2800 : 2000, 80);
    if (priority >= 4) {
        delay(120);
        M5Cardputer.Speaker.tone(2800, 80);
    }
}

void markDirty() { dirty = true; }

bool takeDirty() {
    bool d = dirty;
    dirty = false;
    return d;
}

void toast(const String& t, uint32_t ms) {
    toastText = t;
    toastUntil = millis() + ms;
    if (toastUntil == 0) toastUntil = 1;
    dirty = true;
}

String currentToast() { return toastText; }

}  // namespace core
