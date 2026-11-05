// Live-Systemwerte eines Servers ueber <server_url>/api/v1/system_stats (Nur-Lese-Token, siehe
// auth.h). Alle 5 s neu, Verbindung bleibt dabei offen. Solange die Funktion offen ist,
// dunkelt das Display nicht ab - am Ladekabel als dauerhafte Anzeige nutzbar.
//
// Seiten (, / oder Tab):  Live  ->  Verlauf (CPU, Temperatur)  ->  Backups & Energie
// Felder, die der Server nicht liefern kann (null), werden als "-" angezeigt.

#include <ArduinoJson.h>

#include <vector>

#include "apps.h"
#include "auth.h"
#include "core.h"
#include "net.h"
#include "text.h"
#include "ui.h"

static constexpr uint32_t POLL_MS = 5000;
static constexpr int HISTORY = 116;  // ~10 min bei 5 s

struct Backup {
    String name, lastRun, nextRun;
    bool ok = false, stale = true;
};

struct Disk {
    String name;
    bool mounted = false;
    double total = 0, used = 0;  // Bytes
};

struct Stats {
    bool valid = false;
    float cpu = -1, temp = -1, load1 = -1, powerPi = -1, powerTotal = -1;
    int freq = -1, ramUsed = 0, ramTotal = 0, swapUsed = 0, swapTotal = 0, procs = 0;
    std::vector<float> cores;
    String uptime;
    bool underVoltNow = false, throttledNow = false, tempLimitNow = false, freqCapNow = false;
    bool underVoltEver = false, throttledEver = false;
    double netRx = -1, netTx = -1, diskR = -1, diskW = -1;  // Bytes/s
    float monthKwh = -1, monthCost = -1, yearKwh = -1;
    std::vector<Backup> backups;
    bool hasDisks = false;  // aelterer Server liefert kein "disks"
    std::vector<Disk> disks;
};

static net::Session session;
static Stats st;
static String lastError;
static uint32_t lastPoll = 0;
static int page = 0;
static constexpr int PAGES = 4;

// Zaehler fuer Datenraten
static double prevTs = 0, prevRx = 0, prevTx = 0, prevDr = 0, prevDw = 0;
static float histCpu[HISTORY], histTemp[HISTORY];
static int histLen = 0;

static bool ready() { return !core::cfg.serverUrl.isEmpty() && auth::loggedIn(); }

static float num(JsonVariant v, float fallback = -1) { return v.isNull() ? fallback : v.as<float>(); }

static void pushHistory(float cpu, float temp) {
    if (histLen == HISTORY) {
        memmove(histCpu, histCpu + 1, sizeof(float) * (HISTORY - 1));
        memmove(histTemp, histTemp + 1, sizeof(float) * (HISTORY - 1));
        histLen--;
    }
    histCpu[histLen] = cpu;
    histTemp[histLen] = temp;
    histLen++;
}

static void poll() {
    lastPoll = millis();
    if (!ready()) return;
    JsonDocument d;
    net::Result r = session.getJson(core::cfg.serverUrl + "/api/v1/system_stats", auth::token(), d);
    if (r.status == 401) {
        session.close();
        auth::forget();
        lastError = "abgemeldet - Enter: neu anmelden";
        core::markDirty();
        return;
    }
    if (!r.ok()) {
        lastError = r.code == "scope"      ? "Token hat keinen Zugriff"
                    : r.status == 403      ? "für dieses Konto nicht freigegeben"
                    : r.status == 404      ? "Server kennt system_stats nicht (Update fehlt?)"
                                           : r.error;
        core::markDirty();
        return;
    }
    lastError = "";
    Stats s;
    s.valid = true;
    s.cpu = num(d["cpu_percent"]);
    for (JsonVariant c : d["cpu_percent_per_core"].as<JsonArray>()) s.cores.push_back(c.as<float>());
    s.freq = d["cpu_freq_mhz"].isNull() ? -1 : d["cpu_freq_mhz"].as<int>();
    s.ramUsed = d["ram_used_mb"] | 0;
    s.ramTotal = d["ram_total_mb"] | 0;
    s.swapUsed = d["swap_used_mb"] | 0;
    s.swapTotal = d["swap_total_mb"] | 0;
    s.temp = num(d["temp_c"]);
    s.load1 = num(d["load1"]);
    s.procs = d["process_count"] | 0;
    s.uptime = d["uptime"] | "";
    s.powerPi = num(d["pi_power_watts"]);
    s.powerTotal = num(d["total_power_watts"]);
    JsonObject th = d["throttled"];
    if (!th.isNull()) {
        s.underVoltNow = th["under_voltage_now"] | false;
        s.freqCapNow = th["freq_capped_now"] | false;
        s.throttledNow = th["throttled_now"] | false;
        s.tempLimitNow = th["temp_limit_now"] | false;
        s.underVoltEver = th["under_voltage_occurred"] | false;
        s.throttledEver = th["throttled_occurred"] | false;
    }
    JsonObject en = d["energy"];
    if (!en.isNull()) {
        s.monthKwh = num(en["month_kwh"]);
        s.monthCost = num(en["month_cost"]);
        s.yearKwh = num(en["year_kwh"]);
    }
    // Alle gelieferten Backups; bekannte Schluessel bekommen einen deutschen Namen,
    // sonst wird der Schluessel lesbar gemacht ("foo_backup" -> "foo").
    static const char* const NAMES[][2] = {{"db_backup", "Datenbank"}, {"weekly_mirror", "Spiegelung"},
                                            {"sdcard_image", "SD-Image"}};
    JsonObject bs = d["backup_status"];
    if (!bs.isNull()) {
        for (JsonPair p : bs) {
            JsonObject o = p.value();
            if (o.isNull()) continue;
            Backup x;
            String key = p.key().c_str();
            x.name = "";
            for (auto& n : NAMES)
                if (key == n[0]) x.name = n[1];
            if (x.name.isEmpty()) {
                x.name = key;
                x.name.replace("_backup", "");
                x.name.replace('_', ' ');
                if (x.name.length()) x.name.setCharAt(0, toupper(x.name[0]));
            }
            x.lastRun = o["last_run"] | "nie";
            x.nextRun = o["next_run"] | "";
            x.ok = o["ok"] | false;
            x.stale = o["stale"] | true;
            s.backups.push_back(x);
        }
    }

    JsonArray ds = d["disks"];
    s.hasDisks = !ds.isNull();
    for (JsonObject o : ds) {
        Disk x;
        x.name = text::sanitize(o["name"] | "?");
        x.mounted = o["mounted"] | false;
        x.total = o["total_bytes"] | 0.0;
        x.used = o["used_bytes"] | 0.0;
        s.disks.push_back(x);
    }

    // Datenraten aus der Differenz zweier Abfragen (Zaehler seit Systemstart)
    double ts = d["timestamp"] | 0.0;
    double rx = d["net_recv_bytes"] | 0.0, tx = d["net_sent_bytes"] | 0.0;
    double dr = d["disk_read_bytes"] | 0.0, dw = d["disk_write_bytes"] | 0.0;
    double dt = ts - prevTs;
    if (prevTs > 0 && dt > 0.5 && rx >= prevRx && tx >= prevTx) {
        s.netRx = (rx - prevRx) / dt;
        s.netTx = (tx - prevTx) / dt;
        s.diskR = dr >= prevDr ? (dr - prevDr) / dt : -1;
        s.diskW = dw >= prevDw ? (dw - prevDw) / dt : -1;
    }
    prevTs = ts;
    prevRx = rx;
    prevTx = tx;
    prevDr = dr;
    prevDw = dw;

    pushHistory(s.cpu, s.temp);
    st = std::move(s);
    core::markDirty();
}

static void enter() {
    core::keepAwake = true;
    if (ready()) {
        ui::busy("System", "Verbinde ...");
        poll();
    }
}

static void leave() {
    core::keepAwake = false;
    session.close();  // TLS-Speicher freigeben
}

static void tick() {
    if (ready() && millis() - lastPoll > POLL_MS) poll();
}

// ---------- Zeichnen ----------

static uint16_t loadColor(float pct) {
    if (pct < 0) return ui::C_HINT;
    if (pct >= 90) return ui::C_ERR;
    if (pct >= 70) return ui::C_WARN;
    return ui::C_OK;
}

static uint16_t tempColor(float t) {
    if (t < 0) return ui::C_HINT;
    if (t >= 75) return ui::C_ERR;
    if (t >= 65) return ui::C_WARN;
    return ui::C_OK;
}

static String rate(double bps) {
    if (bps < 0) return "-";
    if (bps < 1024) return String((int)bps) + " B/s";
    if (bps < 1024 * 1024) return text::fmtDecimal(bps / 1024, bps < 10240 ? 1 : 0) + " KB/s";
    return text::fmtDecimal(bps / 1048576, 1) + " MB/s";
}

// Segmentierter Balken: leere Segmente dunkel, gefuellte in der Farbe
static void bar(int x, int y, int w, int h, float pct, uint16_t color) {
    auto& c = ui::canvas;
    int fill = pct > 0 ? (int)(w * min(pct, 100.0f) / 100) : 0;
    for (int i = 0; i < w; i += 3) c.fillRect(x + i, y, min(2, w - i), h, i < fill ? color : ui::C_DIM);
}

// Beschriftung links, Wert rechts in einer Zeile
static void row(int y, const String& label, const String& value, uint16_t color = ui::C_BRIGHT) {
    auto& c = ui::canvas;
    c.setTextColor(ui::C_HINT);
    c.drawString(label, 4, y);
    c.setTextColor(color);
    c.drawString(value, 48, y);
}

static void drawLive() {
    auto& c = ui::canvas;
    const int w = c.width(), L = ui::LINE_H;
    int y = ui::contentTop();

    // CPU: Wert + Balken pro Kern
    String cpu = st.cpu >= 0 ? text::fmtDecimal(st.cpu, 0) + " %" : "-";
    if (st.freq > 0) cpu += "  " + String(st.freq) + " MHz";
    row(y, "CPU", cpu, loadColor(st.cpu));
    int n = st.cores.size();
    if (n) {
        int bx = 150, bw = (w - 4 - bx) / n;
        for (int i = 0; i < n; i++) {
            int h = L - 4;
            int fill = (int)(h * min(st.cores[i], 100.0f) / 100);
            c.fillRect(bx + i * bw, y + 2, bw - 2, h, ui::C_DIM);
            if (fill > 0) c.fillRect(bx + i * bw + 1, y + 2 + h - fill, bw - 4, fill, loadColor(st.cores[i]));
        }
    }
    y += L;

    row(y, "Temp.", st.temp >= 0 ? text::fmtDecimal(st.temp, 1) + " °C" : "-", tempColor(st.temp));
    if (st.load1 >= 0) {
        c.setTextColor(ui::C_HINT);
        String l = "Last " + text::fmtDecimal(st.load1, 2);
        c.drawString(l, w - 4 - c.textWidth(l), y);
    }
    y += L;

    float ramPct = st.ramTotal ? 100.0f * st.ramUsed / st.ramTotal : -1;
    row(y, "RAM", text::fmtDecimal(st.ramUsed / 1024.0, 1) + " / " + text::fmtDecimal(st.ramTotal / 1024.0, 1) + " GB",
        loadColor(ramPct));
    bar(150, y + 3, w - 154, L - 6, ramPct, loadColor(ramPct));
    y += L;

    row(y, "Netz", "ein " + rate(st.netRx) + "  aus " + rate(st.netTx));
    y += L;
    row(y, "Platte", "lesen " + rate(st.diskR) + "  schr. " + rate(st.diskW));
    y += L;

    String pw = st.powerTotal >= 0 ? text::fmtDecimal(st.powerTotal, 1) + " W" : "-";
    if (st.powerPi >= 0) pw += " (Pi " + text::fmtDecimal(st.powerPi, 1) + " W)";
    row(y, "Strom", pw);
    y += L;

    // Warnungen haben Vorrang vor der Laufzeit
    String warn;
    if (st.underVoltNow) warn = "UNTERSPANNUNG";
    else if (st.throttledNow) warn = "gedrosselt";
    else if (st.tempLimitNow) warn = "Temperaturlimit";
    else if (st.freqCapNow) warn = "Takt begrenzt";
    if (!warn.isEmpty()) {
        c.fillRect(0, y, w, L, ui::C_ERR);
        c.setTextColor(ui::C_BG);
        c.setFont(&ui::FONT_BOLD);
        c.drawString(warn, (w - c.textWidth(warn)) / 2, y);
        c.setFont(&ui::FONT);
    } else {
        String info = "läuft " + st.uptime + "   " + String(st.procs) + " Prozesse";
        if (st.underVoltEver || st.throttledEver) info += "   (Warnung seit Start)";
        c.setTextColor(st.underVoltEver || st.throttledEver ? ui::C_WARN : ui::C_HINT);
        c.drawString(ui::fitLine(info, w - 8), 4, y);
    }
}

static void graph(int y, int h, const float* v, float lo, float hi, const String& label, uint16_t color) {
    auto& c = ui::canvas;
    const int x0 = 4, w = c.width() - 8;
    c.drawRect(x0, y, w, h, ui::C_DIM);
    for (int i = 0; i < histLen; i++) {
        if (v[i] < 0) continue;
        int x = x0 + w - 2 - (histLen - 1 - i) * 2;
        int bh = (int)((h - 2) * constrain((v[i] - lo) / (hi - lo), 0.0f, 1.0f));
        if (bh > 0) c.fillRect(x, y + h - 1 - bh, 2, bh, color);
    }
    c.setTextColor(ui::C_BRIGHT);
    c.drawString(label, x0 + 3, y + 1);
}

static void drawHistory() {
    const int L = ui::LINE_H;
    int y = ui::contentTop() + 1;
    int h = (ui::contentBottom(true) - y - 4) / 2;
    String cpu = "CPU " + (st.cpu >= 0 ? text::fmtDecimal(st.cpu, 0) + " %" : String("-"));
    graph(y, h, histCpu, 0, 100, cpu, ui::C_CYAN);
    String t = "Temp. " + (st.temp >= 0 ? text::fmtDecimal(st.temp, 1) + " °C" : String("-")) + "   (30-85 °C)";
    graph(y + h + 4, h, histTemp, 30, 85, t, ui::C_WARN);
    (void)L;
}

static void drawBackups() {
    auto& c = ui::canvas;
    const int w = c.width(), L = ui::LINE_H;
    int y = ui::contentTop();
    if (st.backups.empty()) {
        c.setTextColor(ui::C_HINT);
        c.drawString("Kein Backup-Status geliefert", 4, y + L);
    }
    for (auto& b : st.backups) {
        uint16_t col = !b.ok ? ui::C_ERR : b.stale ? ui::C_WARN : ui::C_OK;
        c.fillRect(5, y + L / 2 - 3, 6, 6, col);
        c.setTextColor(ui::C_BRIGHT);
        c.drawString(ui::fitLine(b.name, 60), 18, y);
        String s = b.lastRun.length() >= 16 ? b.lastRun.substring(0, 6) + " " + b.lastRun.substring(11, 16) : b.lastRun;
        if (b.stale) s += " (überfällig)";
        c.setTextColor(col == ui::C_OK ? ui::C_TEXT : col);
        c.drawString(s, w - 4 - c.textWidth(s), y);
        y += L;
    }
    y += 4;
    if (st.monthKwh >= 0) {
        c.setTextColor(ui::C_HINT);
        c.drawString("Energie Monat", 4, y);
        String e = text::fmtDecimal(st.monthKwh, 2) + " kWh  " + text::fmtDecimal(st.monthCost, 2) + " €";
        c.setTextColor(ui::C_BRIGHT);
        c.drawString(e, w - 4 - c.textWidth(e), y);
        y += L;
        c.setTextColor(ui::C_HINT);
        c.drawString("Energie Jahr", 4, y);
        e = text::fmtDecimal(st.yearKwh, 2) + " kWh";
        c.setTextColor(ui::C_BRIGHT);
        c.drawString(e, w - 4 - c.textWidth(e), y);
    }
}

// Plattengroessen wie df -h (1024er-Schritte): 850 GB, 1,23 TB
static String fmtDisk(double b) {
    const double GB = 1024.0 * 1024 * 1024, TB = GB * 1024;
    if (b >= TB) return text::fmtDecimal(b / TB, b < 10 * TB ? 2 : 1) + " TB";
    return text::fmtDecimal(b / GB, b < 10 * GB ? 1 : 0) + " GB";
}

static void drawDisks() {
    auto& c = ui::canvas;
    const int w = c.width(), L = ui::LINE_H;
    int y = ui::contentTop() + 2;
    if (!st.hasDisks) {
        c.setTextColor(ui::C_HINT);
        for (auto& l : ui::wrap("Der Server liefert noch keine Plattenbelegung (Update von mycloud fehlt).", w - 8)) {
            c.drawString(l, 4, y);
            y += L;
        }
        return;
    }
    // Je Platte: Name und "belegt / gesamt", darunter ein Balken mit Prozent
    for (auto& d : st.disks) {
        c.setTextColor(ui::C_BRIGHT);
        c.setFont(&ui::FONT_BOLD);
        c.drawString(d.name, 4, y);
        c.setFont(&ui::FONT);
        if (!d.mounted || d.total <= 0) {
            c.setTextColor(ui::C_ERR);
            String t = "nicht eingehängt";
            c.drawString(t, w - 4 - c.textWidth(t), y);
            bar(4, y + L, w - 8, 6, 0, ui::C_ERR);
        } else {
            float pct = 100.0f * d.used / d.total;
            uint16_t col = pct >= 90 ? ui::C_ERR : pct >= 80 ? ui::C_WARN : ui::C_OK;
            String t = fmtDisk(d.used) + " / " + fmtDisk(d.total);
            c.setTextColor(ui::C_TEXT);
            c.drawString(t, w - 4 - c.textWidth(t), y);
            String p = text::fmtDecimal(pct, 0) + " %";
            int pw = c.textWidth("100 %");
            bar(4, y + L + 1, w - 14 - pw, 6, pct, col);
            c.setTextColor(col);
            c.drawString(p, w - 4 - c.textWidth(p), y + L - 2);
        }
        y += 2 * L + 4;
    }
}

static void draw() {
    static const char* const TITLES[] = {"System", "System: Verlauf", "System: Backups", "System: Speicher"};
    ui::clear();
    ui::drawHeader(TITLES[page]);
    auto& c = ui::canvas;
    if (!ready()) {
        c.setTextColor(ui::C_TEXT);
        int y = ui::contentTop() + 6;
        String t = core::cfg.serverUrl.isEmpty() ? "Nicht eingerichtet: server_url in /raveneye/config.txt eintragen."
                                                 : "Nicht angemeldet.\n\nEnter: Anmelden (Nur-Lese-Zugang, nur Systemwerte)";
        for (auto& l : ui::wrap(t, c.width() - 8)) {
            c.drawString(l, 4, y);
            y += ui::LINE_H;
        }
        return;
    }
    if (!st.valid) {
        c.setTextColor(lastError.isEmpty() ? ui::C_HINT : ui::C_ERR);
        String t = lastError.isEmpty() ? String("Warte auf Daten ...") : "Fehler: " + lastError;
        int y = ui::contentTop() + 6;
        for (auto& l : ui::wrap(t, c.width() - 8)) {
            c.drawString(l, 4, y);
            y += ui::LINE_H;
        }
        return;
    }
    if (page == 0) drawLive();
    else if (page == 1) drawHistory();
    else if (page == 2) drawBackups();
    else drawDisks();
    if (page != 0) ui::drawHint(lastError.isEmpty() ? ", /  Seite wechseln   ` zurück" : "Fehler: " + lastError);
    else if (!lastError.isEmpty()) ui::drawHint("Fehler: " + lastError);
}

static bool key(const Keys& k) {
    if (k.back) return false;
    if (!ready()) {
        if (k.enter && !core::cfg.serverUrl.isEmpty()) {
            auth::login();
            if (ready()) poll();
        }
        return true;
    }
    if (k.right || k.tab) page = (page + 1) % PAGES;
    else if (k.left) page = (page + PAGES - 1) % PAGES;
    else if (k.has('r')) poll();
    return true;
}

static String badge() {
    if (core::cfg.serverUrl.isEmpty()) return "nicht eingerichtet";
    if (!auth::loggedIn()) return "abgemeldet";
    if (!st.valid) return String();
    return (st.cpu >= 0 ? text::fmtDecimal(st.cpu, 0) + " %" : String()) +
           (st.temp >= 0 ? "  " + text::fmtDecimal(st.temp, 0) + " °C" : String());
}

App systemApp = {"System", enter, key, draw, tick, badge, leave};

namespace systemdaten {
bool bereit() { return ready(); }
Kurz kurz() {
    Kurz k;
    k.gueltig = st.valid;
    k.cpu = st.cpu;
    k.temp = st.temp;
    k.last = st.load1;
    k.ramUsed = st.ramUsed;
    k.ramTotal = st.ramTotal;
    k.unterspannung = st.underVoltNow;
    k.gedrosselt = st.throttledNow || st.tempLimitNow;
    return k;
}
void hintergrund() {
    if (!ready() || !core::online() || !core::timeValid()) return;
    if (millis() - lastPoll > 15000) poll();   // Verbindung bleibt offen (wie in der Funktion selbst)
}
}  // namespace systemdaten
