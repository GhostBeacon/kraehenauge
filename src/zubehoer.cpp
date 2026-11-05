#include "zubehoer.h"

#include <Beiwerk.h>
#include <M5Cardputer.h>
#include <time.h>

#include "apps.h"
#include "core.h"
#include "text.h"
#include "ui.h"

namespace zub {

using namespace beiwerk;

enum HexAnsicht : uint8_t { H_LAMPE, H_RUHIG, H_ANZAHL };
enum OledAnsicht : uint8_t { O_WECHSEL, O_SYSTEM, O_STATUS, O_MELDUNG, O_ANZAHL };
static const char* const HEX_NAMEN[H_ANZAHL] = {"Lampe", "Ruhig"};
static const char* const OLED_NAMEN[O_ANZAHL] = {"Wechsel", "System", "Status", "Meldung"};
static const uint8_t HELL_STUFEN[3] = {5, 12, 25};   // Prozent; Obergrenze der Bibliothek ist 40
static constexpr uint32_t WECHSEL_MS = 8000;

static uint8_t ansichtHex = H_LAMPE, ansichtOled = O_WECHSEL, stufe = 1;
static bool gedreht = false;

static M5Canvas* bild = nullptr;   // 128x64, 8 bit grau; wird nur bei Aenderung gesendet
static uint8_t letztes[128 * 64];

static String letzteId;            // neueste bekannte Meldung (fuer den Blitz bei neuen)
static uint32_t blitzBis = 0;
static uint32_t blitzFarbe = 0;

// ---------------------------------------------------------------- Einstellungen
static void speichern() {
    uint8_t b[4] = {ansichtHex, ansichtOled, stufe, (uint8_t)gedreht};
    core::prefs.putBytes("zub", b, sizeof(b));
}

static void laden() {
    uint8_t b[4];
    if (core::prefs.getBytes("zub", b, sizeof(b)) == sizeof(b)) {
        ansichtHex = b[0] % H_ANZAHL;
        ansichtOled = b[1] % O_ANZAHL;
        stufe = b[2] % 3;
        gedreht = b[3];
    }
}

// ---------------------------------------------------------------- Bedienung
String geraetName() { return geraet() == Geraet::Oled ? "OLED" : "Hex"; }

String ansichtName() { return geraet() == Geraet::Oled ? OLED_NAMEN[ansichtOled] : HEX_NAMEN[ansichtHex]; }

const char* erkennen() {
    Geraet g = beiwerk::erkennen();
    memset(letztes, 0xAA, sizeof(letztes));   // naechstes OLED-Bild auf jeden Fall senden
    if (g == Geraet::Oled) {
        if (!bild) {
            bild = new M5Canvas(&oled());
            bild->setColorDepth(8);
            bild->createSprite(128, 64);
        }
        return "Zubehör: OLED erkannt";
    }
    return "Zubehör: Hex aktiv (kein OLED gefunden)";
}

const char* naechsteAnsicht() {
    static char b[40];
    if (geraet() == Geraet::Oled) ansichtOled = (ansichtOled + 1) % O_ANZAHL;
    else ansichtHex = (ansichtHex + 1) % H_ANZAHL;
    speichern();
    snprintf(b, sizeof(b), "%s: %s", geraetName().c_str(), ansichtName().c_str());
    return b;
}

const char* helligkeitOderDrehen() {
    static char b[40];
    if (geraet() == Geraet::Oled) {
        gedreht = !gedreht;
        oledDrehen(gedreht);
        memset(letztes, 0xAA, sizeof(letztes));
        snprintf(b, sizeof(b), "OLED %s", gedreht ? "gedreht" : "normal");
    } else {
        stufe = (stufe + 1) % 3;
        hex::helligkeit(HELL_STUFEN[stufe]);
        snprintf(b, sizeof(b), "Hex-Helligkeit %d/3", stufe + 1);
    }
    speichern();
    return b;
}

bool taste(const Keys& k) {
    if (!k.fn) return false;
    const char* m = nullptr;
    if (k.has('e')) m = erkennen();
    else if (k.has('r')) m = naechsteAnsicht();
    else if (k.has('t')) m = helligkeitOderDrehen();
    if (!m) return false;
    core::toast(m, 2500);
    return true;
}

void begin() {
    laden();
    hex::helligkeit(HELL_STUFEN[stufe]);
    oledDrehen(gedreht);
    erkennen();
}

// ---------------------------------------------------------------- Meldungen
static uint32_t prioFarbe(uint8_t p) {
    switch (p) {
        case 5: return 0xFF0010;
        case 4: return 0xFF6000;
        case 1:
        case 2: return 0x406080;
        default: return 0x05D9E8;
    }
}

// Neue Meldung (nicht aus dem 12-h-Nachladen beim Start): kurzer Blitz am Hex
static void neueMeldungPruefen() {
    if (core::messages.empty()) return;
    const NtfyMessage& m = core::messages.front();
    if (m.id == letzteId) return;
    letzteId = m.id;
    if (core::timeValid() && (int64_t)time(nullptr) - (int64_t)m.time < 120) {
        blitzBis = millis() + 2500;
        blitzFarbe = prioFarbe(m.priority);
    }
}

static const NtfyMessage* ungelesen() {
    for (auto& m : core::messages)
        if (!m.seen) return &m;
    return nullptr;
}

// ---------------------------------------------------------------- Hex
static uint32_t mal(uint32_t rgb, float f) { return skaliere(rgb, (uint8_t)(constrain(f, 0.0f, 1.0f) * 255)); }

static int dienstLed(int i) { return i < 12 ? hex::ring(2, i) : hex::ring(3, i - 12); }   // 30 Plaetze

static void hexBild(bool ruhig) {
    uint32_t now = millis();
    bool blink = (now / 500) % 2;
    float atem = 0.55f + 0.45f * sinf(now / 600.0f);
    hex::clear();

    // Mitte: ungelesene Meldung (atmet in der Prioritaetsfarbe), sonst Verbindung
    if (const NtfyMessage* m = ungelesen()) {
        hex::set(hex::ring(0, 0), mal(prioFarbe(m->priority), atem));
    } else if (!ruhig) {
        uint32_t c = core::ntfy.state() == NtfyClient::State::Streaming ? 0x00FF40
                     : core::online()                                    ? 0xFFC000
                                                                         : 0xFF0000;
        hex::set(hex::ring(0, 0), mal(c, 0.15f));
    } else if (!core::online()) {
        hex::set(hex::ring(0, 0), mal(0xFF0000, blink ? 0.4f : 0.0f));
    }

    // Innerer Ring: Server-Temperatur (40-80 °C auf 6 LEDs), Warnung blinkt rot
    systemdaten::Kurz s = systemdaten::kurz();
    if (s.gueltig) {
        if (s.unterspannung || s.gedrosselt) {
            hex::ringFill(1, blink ? 0xFF0000 : 0);
        } else if (!ruhig && s.temp >= 0) {
            int n = constrain((int)((s.temp - 40) / 40 * 6 + 0.5f), 1, 6);
            uint32_t c = s.temp >= 75 ? 0xFF0000 : s.temp >= 65 ? 0xFF6000 : 0x00FF40;
            for (int k = 0; k < n; k++) hex::set(hex::ring(1, k), mal(c, 0.35f));
        }
    }

    // Aussen: je Dienst eine LED
    int n = min(statusdaten::anzahl(), 30);
    for (int i = 0; i < n; i++) {
        int z = statusdaten::zustand(i);
        uint32_t c = 0;
        if (z == 0) c = blink ? 0xFF0000 : 0x200000;
        else if (ruhig) continue;
        else if (z == 1) c = mal(0x00FF40, 0.2f);
        else if (z == 2) c = mal(0xFF6000, 0.6f);
        else if (z == 3) c = mal(0x2050FF, 0.4f);
        else c = 0x080808;
        hex::set(dienstLed(i), c);
    }

    // Neue Meldung: ein Komet kreist 2,5 s im aeusseren Ring
    if ((int32_t)(blitzBis - now) > 0) {
        int kopf = (now / 40) % 18;
        for (int k = 0; k < 6; k++) hex::add(hex::ring(3, kopf - k), mal(blitzFarbe, 1.0f - k * 0.16f));
    }
    hex::show();
}

// ---------------------------------------------------------------- OLED
static void titel(M5Canvas& c, const String& links, const String& rechts) {
    c.setFont(&ui::FONT_BOLD);
    c.drawString(links, 0, 0);
    c.setFont(&ui::FONT);
    c.drawString(rechts, 128 - c.textWidth(rechts), 0);
    c.drawFastHLine(0, 13, 128, TFT_WHITE);
}

static String uhrzeit() { return core::timeValid() ? text::fmtTime(time(nullptr)) : String(); }

static void oledSystem(M5Canvas& c) {
    titel(c, "SYSTEM", uhrzeit());
    if (!systemdaten::bereit()) {
        c.drawString(core::cfg.serverUrl.isEmpty() ? "nicht eingerichtet" : "nicht angemeldet", 0, 24);
        c.drawString("Krähenauge: 3 System", 0, 38);
        return;
    }
    systemdaten::Kurz s = systemdaten::kurz();
    if (!s.gueltig) {
        c.drawString("warte auf Daten ...", 0, 24);
        return;
    }
    c.setFont(&ui::FONT_BOLD);
    c.setTextSize(2);
    c.drawString(s.cpu >= 0 ? String((int)(s.cpu + 0.5f)) + "%" : String("-"), 0, 15);
    String t = s.temp >= 0 ? String((int)(s.temp + 0.5f)) + "°" : String("-");
    c.drawString(t, 128 - c.textWidth(t), 15);
    c.setTextSize(1);
    c.setFont(&ui::FONT);
    c.drawString("CPU", 0, 40);
    c.drawString("Temp.", 128 - c.textWidth("Temp."), 40);
    if (s.unterspannung || s.gedrosselt) {
        c.fillRect(0, 52, 128, 12, TFT_WHITE);
        c.setTextColor(TFT_BLACK);
        String w = s.unterspannung ? "UNTERSPANNUNG" : "GEDROSSELT";
        c.drawString(w, (128 - c.textWidth(w)) / 2, 52);
        c.setTextColor(TFT_WHITE);
    } else {
        String r = "RAM " + text::fmtDecimal(s.ramUsed / 1024.0, 1) + "/" + text::fmtDecimal(s.ramTotal / 1024.0, 1) + " GB";
        if (s.last >= 0) r += "  L" + text::fmtDecimal(s.last, 1);
        c.drawString(r, 0, 52);
    }
}

static void oledStatus(M5Canvas& c) {
    int n = statusdaten::anzahl(), ok = 0;
    for (int i = 0; i < n; i++) ok += statusdaten::zustand(i) == 1 || statusdaten::zustand(i) == 3;
    titel(c, "STATUS", n ? String(ok) + "/" + n + " ok" : String());
    if (!statusdaten::eingerichtet()) {
        c.drawString("nicht eingerichtet", 0, 24);
        return;
    }
    if (!n) {
        c.drawString("noch keine Daten", 0, 24);
        return;
    }
    if (ok == n) {
        c.setFont(&ui::FONT_BOLD);
        c.setTextSize(2);
        c.drawCenterString("ALLES OK", 64, 24);
        c.setTextSize(1);
        c.setFont(&ui::FONT);
        return;
    }
    int y = 16;
    for (int i = 0; i < n && y <= 52; i++) {
        int z = statusdaten::zustand(i);
        if (z == 1 || z == 3) continue;
        String zeile = String(z == 0 ? "AUS " : "?   ") + statusdaten::name(i);
        c.drawString(ui::fitLine(zeile, 128), 0, y);
        y += 12;
    }
}

static void oledMeldung(M5Canvas& c) {
    if (core::messages.empty()) {
        titel(c, "MELDUNG", uhrzeit());
        c.drawString("keine Meldungen", 0, 24);
        return;
    }
    const NtfyMessage& m = core::messages.front();
    int u = core::unread();
    titel(c, u ? "NEU (" + String(u) + ")" : String("MELDUNG"), text::fmtTime(m.time));
    int y = 15;
    if (!m.title.isEmpty()) {
        c.setFont(&ui::FONT_BOLD);
        c.drawString(ui::fitLine(m.title, 128), 0, y);
        c.setFont(&ui::FONT);
        y += 12;
    }
    // ui::wrap misst mit der Schrift der Hauptleinwand - dieselbe wie hier
    for (auto& l : ui::wrap(m.message, 128)) {
        if (y > 52) break;
        c.drawString(l, 0, y);
        y += 12;
    }
}

static void oledBild() {
    M5Canvas& c = *bild;
    c.fillSprite(TFT_BLACK);
    c.setTextColor(TFT_WHITE);
    c.setFont(&ui::FONT);
    c.setTextSize(1);
    uint8_t a = ansichtOled;
    if (a == O_WECHSEL) a = O_SYSTEM + (millis() / WECHSEL_MS) % 3;
    if (a == O_SYSTEM) oledSystem(c);
    else if (a == O_STATUS) oledStatus(c);
    else oledMeldung(c);

    uint8_t* buf = (uint8_t*)c.getBuffer();
    if (memcmp(buf, letztes, sizeof(letztes)) != 0) {
        memcpy(letztes, buf, sizeof(letztes));
        c.pushSprite(0, 0);   // ~25 ms ueber I2C, daher nur bei Aenderung
    }
}

// ---------------------------------------------------------------- Takt
void tick() {
    neueMeldungPruefen();

    // Hintergrundabfragen nur, wenn die Anzeige sie braucht
    bool hex = geraet() == Geraet::Hex;
    bool oled = geraet() == Geraet::Oled;
    uint8_t a = ansichtOled;
    if (hex || (oled && (a == O_WECHSEL || a == O_STATUS))) statusdaten::hintergrund();
    if (hex || (oled && (a == O_WECHSEL || a == O_SYSTEM))) systemdaten::hintergrund();

    static uint32_t lastHex = 0, lastOled = 0;
    uint32_t now = millis();
    if (hex && now - lastHex >= 33) {
        lastHex = now;
        hexBild(ansichtHex == H_RUHIG);
    } else if (oled && bild && now - lastOled >= 250) {
        lastOled = now;
        oledBild();
    }
}

}  // namespace zub
