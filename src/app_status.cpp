// Statusanzeige aus einer Uptime-Kuma-Statusseite (Version 1.x, oeffentliche API):
//   GET <status_url>/api/status-page/<slug>            -> Gruppen + Monitore (Namen)
//   GET <status_url>/api/status-page/heartbeat/<slug>  -> je Monitor die letzten 50 Messungen
//                                                         + Verfuegbarkeit 24 h ("<id>_24")
// Status: 0 = ausgefallen, 1 = laeuft, 2 = ausstehend, 3 = Wartung

#include <ArduinoJson.h>

#include <vector>

#include "apps.h"
#include "core.h"
#include "net.h"
#include "text.h"
#include "ui.h"

static constexpr uint32_t REFRESH_MS = 60000;

struct Monitor {
    int id = 0;
    String name;
    String group;
    int status = -1;       // letzte Messung, -1 = keine Daten
    int ping = -1;         // ms
    double uptime = -1;    // 0..1
    std::vector<int8_t> beats;  // aelteste zuerst
};

static std::vector<Monitor> monitors;
static String lastError;
static uint32_t lastFetch = 0;
static bool fetched = false;
static int sel = 0, top = 0;

static bool configured() { return !core::cfg.statusUrl.isEmpty() && !core::cfg.statusSlug.isEmpty(); }

static uint16_t statusColor(int s) {
    switch (s) {
        case 0: return ui::C_ERR;
        case 1: return ui::C_OK;
        case 2: return ui::C_WARN;
        case 3: return ui::C_INFO;  // Wartung
        default: return ui::C_HINT;
    }
}

static const char* statusText(int s) {
    switch (s) {
        case 0: return "ausgefallen";
        case 1: return "läuft";
        case 2: return "ausstehend";
        case 3: return "Wartung";
        default: return "keine Daten";
    }
}

static void fetch() {
    lastFetch = millis();
    if (!configured()) return;
    String base = core::cfg.statusUrl + "/api/status-page/";

    JsonDocument filter;
    filter["publicGroupList"][0]["name"] = true;
    filter["publicGroupList"][0]["monitorList"][0]["id"] = true;
    filter["publicGroupList"][0]["monitorList"][0]["name"] = true;
    JsonDocument page;
    net::Result r = net::getJson(base + core::cfg.statusSlug, "", page, &filter);
    if (!r.ok()) {
        lastError = r.status == 404 ? "Statusseite nicht gefunden" : r.error;
        core::markDirty();
        return;
    }

    std::vector<Monitor> list;
    for (JsonObject g : page["publicGroupList"].as<JsonArray>()) {
        for (JsonObject m : g["monitorList"].as<JsonArray>()) {
            Monitor mon;
            mon.id = m["id"] | 0;
            mon.name = text::sanitize(m["name"] | "");
            mon.group = text::sanitize(g["name"] | "");
            list.push_back(mon);
        }
    }
    page.clear();

    JsonDocument hbFilter;
    hbFilter["heartbeatList"]["*"][0]["status"] = true;
    hbFilter["heartbeatList"]["*"][0]["ping"] = true;
    hbFilter["uptimeList"] = true;
    JsonDocument hb;
    r = net::getJson(base + "heartbeat/" + core::cfg.statusSlug, "", hb, &hbFilter);
    if (!r.ok()) {
        lastError = r.error;
        core::markDirty();
        return;
    }
    for (auto& mon : list) {
        JsonArray beats = hb["heartbeatList"][String(mon.id)].as<JsonArray>();
        for (JsonObject b : beats) mon.beats.push_back(b["status"] | -1);
        if (beats.size()) {
            JsonObject last = beats[beats.size() - 1];
            mon.status = last["status"] | -1;
            mon.ping = last["ping"].isNull() ? -1 : (int)round(last["ping"].as<float>());
        }
        JsonVariant up = hb["uptimeList"][String(mon.id) + "_24"];
        if (!up.isNull()) mon.uptime = up.as<double>();
    }
    monitors = std::move(list);
    lastError = "";
    fetched = true;
    if (sel >= (int)monitors.size()) sel = max(0, (int)monitors.size() - 1);
    core::markDirty();
}

static String summary() {
    if (monitors.empty()) return String();
    int up = 0;
    for (auto& m : monitors) up += m.status == 1 || m.status == 3;
    return String(up) + "/" + monitors.size() + " ok";
}

static void enter() {
    if (!configured()) return;
    if (!fetched || millis() - lastFetch > REFRESH_MS) {
        ui::busy("Status", "Lade Statusseite ...");
        fetch();
    }
}

static void tick() {
    if (configured() && millis() - lastFetch > REFRESH_MS) fetch();
}

static void draw() {
    ui::clear();
    ui::drawHeader("Status");
    auto& c = ui::canvas;
    const int w = c.width();

    if (!configured()) {
        c.setTextColor(ui::C_TEXT);
        int y = ui::contentTop() + 4;
        for (auto& l : ui::wrap("Nicht eingerichtet. In /raveneye/config.txt eintragen:\nstatus_url=https://...\nstatus_slug=...", w - 8)) {
            c.drawString(l, 4, y);
            y += ui::LINE_H;
        }
        return;
    }

    String s = summary();
    ui::drawHint(!lastError.isEmpty() ? "Fehler: " + lastError
                                      : (s.isEmpty() ? String() : s + "   ") + "Enter: Verlauf   r: neu laden");
    if (monitors.empty()) {
        c.setTextColor(ui::C_HINT);
        const char* t = fetched ? "Keine Monitore auf der Statusseite" : "Noch keine Daten";
        c.drawString(t, (w - c.textWidth(t)) / 2, c.height() / 2 - ui::LINE_H);
        return;
    }

    const int rowH = ui::LINE_H + 2;
    ui::drawList(monitors.size(), sel, top, ui::contentTop(), ui::contentBottom(true), rowH, [&](int i, int y, bool s) {
        const auto& m = monitors[i];
        if (s) c.fillRect(6, y + rowH / 2 - 4, 8, 8, ui::C_BG);  // Rahmen, damit Gruen auf Cyan sichtbar bleibt
        c.fillRect(7, y + rowH / 2 - 3, 6, 6, statusColor(m.status));
        String right = m.uptime >= 0 ? text::fmtDecimal(m.uptime * 100, m.uptime >= 0.9995 ? 0 : 1) + " %" : "";
        int rw = c.textWidth(right);
        c.setTextColor(ui::ink(s, m.status == 0 ? ui::C_ERR : ui::C_BRIGHT));
        c.drawString(ui::fitLine(m.name, w - 30 - rw), 18, y + 2);
        c.setTextColor(ui::ink(s, ui::C_HINT));
        c.drawString(right, w - 8 - rw, y + 2);
    });
}

static void showDetail(const Monitor& m) {
    auto& c = ui::canvas;
    auto drawDetail = [&]() {
        ui::clear();
        ui::drawHeader(m.name.c_str());
        int y = ui::contentTop() + 4;
        c.setTextColor(statusColor(m.status));
        c.setFont(&ui::FONT_BOLD);
        c.drawString(statusText(m.status), 4, y);
        c.setFont(&ui::FONT);
        y += ui::LINE_H + 2;
        c.setTextColor(ui::C_TEXT);
        if (!m.group.isEmpty()) {
            c.drawString("Gruppe: " + m.group, 4, y);
            y += ui::LINE_H;
        }
        String line = "24 h: " + (m.uptime >= 0 ? text::fmtDecimal(m.uptime * 100, 2) + " %" : String("-"));
        if (m.ping >= 0) line += "    Antwort: " + String(m.ping) + " ms";
        c.drawString(line, 4, y);
        y += ui::LINE_H + 6;

        // Verlauf: ein Balken pro Messung, neueste rechts
        int n = m.beats.size();
        if (n) {
            // Kuma 1 liefert 50, Kuma 2 100 Messungen: Breite an die Anzahl anpassen,
            // reicht der Platz nicht, nur die neuesten zeigen
            int bw = max(2, (c.width() - 8) / n);
            int shown = min(n, (c.width() - 8) / bw);
            int x = c.width() - 4 - shown * bw;
            for (int i = 0; i < shown; i++) c.fillRect(x + i * bw, y, max(1, bw - 1), 14, statusColor(m.beats[n - shown + i]));
            n = shown;
            c.drawFastHLine(4, y + 15, c.width() - 8, ui::C_DIM);
            y += 18;
            c.setTextColor(ui::C_HINT);
            c.drawString("letzte " + String(n) + " Messungen", 4, y);
        }
        ui::drawHint("` / Del: zurück");
        ui::push();
    };
    Keys k;
    drawDetail();
    while (true) {
        core::tick();
        if (keys::poll(k) && (k.back || k.enter)) return;
        if (core::takeDirty()) drawDetail();
        delay(5);
    }
}

static bool key(const Keys& k) {
    if (k.back) return false;
    if (k.has('r') && configured()) {
        ui::busy("Status", "Lade Statusseite ...");
        fetch();
        return true;
    }
    if (monitors.empty()) return true;
    if (k.up && sel > 0) sel--;
    else if (k.down && sel + 1 < (int)monitors.size()) sel++;
    else if (k.left) sel = max(0, sel - 5);
    else if (k.right) sel = min((int)monitors.size() - 1, sel + 5);
    else if (k.enter) showDetail(monitors[sel]);
    return true;
}

static String badge() {
    if (!configured()) return "nicht eingerichtet";
    return summary();
}

App statusApp = {"Status", enter, key, draw, tick, badge};

namespace statusdaten {
bool eingerichtet() { return configured(); }
int anzahl() { return monitors.size(); }
int zustand(int i) { return i >= 0 && i < (int)monitors.size() ? monitors[i].status : -1; }
String name(int i) { return i >= 0 && i < (int)monitors.size() ? monitors[i].name : String(); }
void hintergrund() {
    if (!configured() || !core::online() || !core::timeValid()) return;
    uint32_t warte = fetched ? REFRESH_MS : 15000;   // noch keine Daten: bald nach dem Verbinden holen
    if (millis() - lastFetch > warte) fetch();
}
}  // namespace statusdaten
