// Startbild: Stoerstreifen, dann "KRÄHENAUGE" mit RGB-Versatz (Glitch), Fortschrittsbalken und
// dem echten Verbindungsstand darunter. Laeuft ~2,6 s, jede Taste ueberspringt.
// WLAN, Uhrzeit und ntfy starten waehrenddessen schon (core::tick).

#include "boot.h"

#include "core.h"
#include "keys.h"
#include "ui.h"

namespace boot {

static constexpr uint32_t NOISE_MS = 400;
static constexpr uint32_t TOTAL_MS = 2600;

// Was gerade passiert - kein Fake-Log, sondern der wirkliche Zustand
static String stage() {
    if (!core::online()) return "> wlan scan";
    if (!core::timeValid()) return "> ntp sync";
    if (!core::live()) {
        String n = core::quelle().name();
        n.toLowerCase();
        return "> " + n + " link";
    }
    return "> online";
}

static void noise(M5Canvas& c) {
    c.fillScreen(ui::C_BG);
    for (int i = 0; i < 7; i++) {
        uint16_t col = random(2) ? ui::C_CYAN : ui::C_MAGENTA;
        int y = random(c.height()), x = random(c.width() / 2);
        c.drawFastHLine(x, y, random(40, c.width() - x), col);
    }
    if (random(3) == 0) c.fillRect(random(c.width() - 30), random(c.height() - 6), random(8, 30), 3, ui::C_DIM);
}

static void frame(M5Canvas& c, uint32_t t, const char* version) {
    const int w = c.width(), cx = w / 2;
    c.fillScreen(ui::C_BG);

    // Titel dreifach: magenta links, cyan rechts, hell darueber; ab und zu springt der Versatz
    int j = random(100) < 12 ? random(-3, 4) : 0;
    c.setFont(&ui::FONT);  // Pixelschrift dreifach vergroessert: grobe Pixel, keine Extra-Schrift im Flash
    c.setTextSize(3);
    c.setTextDatum(middle_center);
    c.setTextColor(ui::C_MAGENTA);
    c.drawString("KRÄHENAUGE", cx - 2 + j, 44);
    c.setTextColor(ui::C_CYAN);
    c.drawString("KRÄHENAUGE", cx + 2 - j, 44);
    c.setTextColor(ui::C_BRIGHT);
    c.drawString("KRÄHENAUGE", cx, 44);
    if (j) c.fillRect(0, 30 + random(28), w, 2, ui::C_BG);  // Riss durch den Schriftzug

    c.setTextSize(1);
    c.setTextColor(ui::C_HINT);
    c.drawString(String("// watching the net  v") + version, cx, 74);
    c.setTextDatum(top_left);

    // Segmentierter Fortschrittsbalken
    const int bx = 40, by = 96, bw = w - 80;
    c.drawRect(bx, by, bw, 8, ui::C_CYAN);
    int fill = (bw - 4) * min<uint32_t>(t - NOISE_MS, TOTAL_MS - NOISE_MS) / (TOTAL_MS - NOISE_MS);
    for (int x = 0; x < fill; x += 4) c.fillRect(bx + 2 + x, by + 2, min(3, fill - x), 4, ui::C_CYAN);

    c.setTextColor(ui::C_MAGENTA);
    c.drawString(stage(), bx, by + 14);
}

void run(const char* version) {
    auto& c = ui::canvas;
    const uint32_t start = millis();
    Keys k;
    while (true) {
        uint32_t t = millis() - start;
        if (t >= TOTAL_MS) break;
        core::tick();
        if (keys::poll(k)) break;
        if (t < NOISE_MS) noise(c);
        else frame(c, t, version);
        c.pushSprite(0, 0);
        delay(30);
    }
    core::takeDirty();
}

}  // namespace boot
