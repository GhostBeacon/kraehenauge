// Einstellungen und Geraeteinfo. Enter aendert bzw. zeigt Details.

#include <WiFi.h>

#include "apps.h"
#include "auth.h"
#include "core.h"
#include "text.h"
#include "ui.h"
#include "zubehoer.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

static int sel = 0, top = 0;

static const uint8_t BRIGHT_STEPS[] = {32, 64, 128, 192, 255};
static const uint32_t DIM_STEPS[] = {30000, 60000, 120000, 300000, 0};

enum Row { R_SOUND, R_BRIGHT, R_DIM, R_WIFI, R_SERVER, R_ZUB, R_SYSTEM, R_RESTART, R_COUNT };

static String label(int r) {
    switch (r) {
        case R_SOUND: return "Ton";
        case R_BRIGHT: return "Helligkeit";
        case R_DIM: return "Abdunkeln nach";
        case R_WIFI: return "WLAN";
        case R_SERVER: return "Server";
        case R_ZUB: return "Zubehör";
        case R_SYSTEM: return "System";
        case R_RESTART: return "Neu starten";
    }
    return "";
}

static String value(int r) {
    auto& s = core::settings;
    switch (r) {
        case R_SOUND: return s.muted ? "aus" : "an";
        case R_BRIGHT: return String((s.brightness * 100 + 127) / 255) + " %";
        case R_DIM:
            if (!s.dimAfterMs) return "nie";
            return s.dimAfterMs < 60000 ? String(s.dimAfterMs / 1000) + " s" : String(s.dimAfterMs / 60000) + " min";
        case R_WIFI: return core::online() ? text::sanitize(core::wifiSsid()) : String("getrennt");
        case R_SERVER:
            if (core::cfg.serverUrl.isEmpty()) return "nicht eingerichtet";
            return auth::loggedIn() ? text::sanitize(auth::user()) : String("abgemeldet");
        case R_ZUB: return zub::geraetName() + ": " + zub::ansichtName();
        case R_SYSTEM: return "v" FW_VERSION "  RAM " + String(ESP.getFreeHeap() / 1024) + " KB";
        default: return "";
    }
}

static void draw() {
    ui::clear();
    ui::drawHeader("Einstellungen");
    auto& c = ui::canvas;
    const int w = c.width(), rowH = ui::LINE_H + 1;
    ui::drawList(R_COUNT, sel, top, ui::contentTop(), ui::contentBottom(false), rowH, [&](int i, int y, bool s) {
        c.setTextColor(ui::ink(s, ui::C_BRIGHT));
        c.drawString(label(i), 7, y + 1);
        String v = value(i);
        c.setTextColor(ui::ink(s, ui::C_CYAN));
        v = ui::fitLine(v, w / 2);
        c.drawString(v, w - 8 - c.textWidth(v), y + 1);
    });
}

template <typename T, size_t N>
static T nextStep(const T (&steps)[N], T current) {
    for (size_t i = 0; i < N; i++)
        if (steps[i] == current) return steps[(i + 1) % N];
    return steps[0];
}

static void activate(int r) {
    auto& s = core::settings;
    switch (r) {
        case R_SOUND:
            s.muted = !s.muted;
            core::saveSettings();
            break;
        case R_BRIGHT:
            s.brightness = nextStep(BRIGHT_STEPS, s.brightness);
            core::applyBrightness();
            core::saveSettings();
            break;
        case R_DIM:
            s.dimAfterMs = nextStep(DIM_STEPS, s.dimAfterMs);
            core::saveSettings();
            break;
        case R_WIFI: {
            String t;
            if (core::online()) {
                t = "Netz: " + text::sanitize(core::wifiSsid()) + "\nSignal: " + String(core::wifiRssi()) + " dBm\nIP: " +
                    WiFi.localIP().toString();
            } else {
                t = "Nicht verbunden.";
            }
            t += "\n\nBekannte Netze (config.txt):";
            for (auto& n : core::cfg.wifis) t += "\n- " + text::sanitize(n.ssid);
            t += "\n\nMAC: " + WiFi.macAddress();
            t += "\n\nLetzte Ereignisse:\n" + text::sanitize(core::wifiDiagnosis());
            ui::message("WLAN", t);
            break;
        }
        case R_SERVER:
            if (core::cfg.serverUrl.isEmpty()) {
                ui::message("Server", "server_url fehlt in /raveneye/config.txt.");
            } else if (!auth::loggedIn()) {
                auth::login();
            } else if (ui::confirm("Server", "Abmelden? Das Token wird auch auf dem Server gelöscht.")) {
                auth::logout();
            }
            break;
        case R_ZUB: {
            String extra = zub::geraetName() == "OLED" ? "OLED drehen" : "Hex-Helligkeit ändern";
            int c = ui::choose("Zubehör", {"Neu erkennen", "Ansicht wechseln (" + zub::ansichtName() + ")", extra});
            const char* m = c == 0 ? zub::erkennen() : c == 1 ? zub::naechsteAnsicht() : c == 2 ? zub::helligkeitOderDrehen() : nullptr;
            if (m) core::toast(m, 2500);
            break;
        }
        case R_SYSTEM:
            ui::message("System", "Krähenauge " FW_VERSION "\nRAM frei: " + String(ESP.getFreeHeap() / 1024) +
                                      " KB\nkleinster Stand: " + String(ESP.getMinFreeHeap() / 1024) +
                                      " KB\ngrößter Block: " + String(ESP.getMaxAllocHeap() / 1024) + " KB\nLaufzeit: " +
                                      String(millis() / 60000) + " min\nAkku: " + String(M5Cardputer.Power.getBatteryLevel()) + " %");
            break;
        case R_RESTART:
            if (ui::confirm("Neu starten", "Jetzt neu starten? (z. B. nach Änderung der config.txt)")) ESP.restart();
            break;
    }
}

static bool key(const Keys& k) {
    if (k.back) return false;
    if (k.up && sel > 0) sel--;
    else if (k.down && sel + 1 < R_COUNT) sel++;
    else if (k.enter) activate(sel);
    return true;
}

App settingsApp = {"Einstellungen", nullptr, key, draw, nullptr, nullptr};
