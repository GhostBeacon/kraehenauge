// Meldungen (Meldestelle oder ntfy): Liste (neueste oben), Enter oeffnet, x entfernt (nur auf dem Geraet)

#include "apps.h"
#include "core.h"
#include "text.h"
#include "ui.h"

static String selectedId;  // Auswahl per ID - neue Meldungen schieben die Liste nach unten
static int top = 0;

static int selectedIndex() {
    for (size_t i = 0; i < core::messages.size(); i++)
        if (core::messages[i].id == selectedId) return i;
    return 0;
}

static void select(int i) {
    if (core::messages.empty()) return;
    i = constrain(i, 0, (int)core::messages.size() - 1);
    selectedId = core::messages[i].id;
}

static void enter() {
    core::ntfyVisible = true;
    select(0);
    top = 0;
}

static void draw() {
    ui::clear();
    ui::drawHeader("Meldungen");
    auto& c = ui::canvas;
    const int w = c.width();

    if (core::messages.empty()) {
        c.setTextColor(ui::C_HINT);
        String hint = !core::online()                   ? String("Verbinde mit WLAN ...")
                      : core::live()                     ? String("Keine Meldungen")
                      : !core::quelle().hinweis().isEmpty() ? core::quelle().hinweis()
                                                         : "Verbinde mit " + String(core::quelle().name()) + " ...";
        hint = ui::fitLine(hint, w - 8);
        c.drawString(hint, (w - c.textWidth(hint)) / 2, c.height() / 2);
        return;
    }

    const int rowH = 2 * ui::LINE_H + 2;
    ui::drawList(core::messages.size(), selectedIndex(), top, ui::contentTop(), ui::contentBottom(false), rowH,
                 [&](int i, int y, bool s) {
                     const auto& m = core::messages[i];
                     c.fillRect(0, y + 2, 3, rowH - 4, ui::prioColor(m.priority));
                     if (!m.seen) c.fillRect(w - 9, y + ui::LINE_H / 2 - 1, 4, 4, ui::ink(s, ui::C_MAGENTA));

                     String t = text::fmtTime(m.time);
                     int tw = c.textWidth(t);
                     c.setTextColor(ui::ink(s, ui::C_HINT));
                     c.drawString(t, w - 14 - tw, y + 2);

                     String head = m.title.isEmpty() ? m.topic : m.title;
                     c.setTextColor(ui::ink(s, ui::prioColor(m.priority)));
                     c.setFont(&ui::FONT_BOLD);
                     c.drawString(ui::fitLine(head, w - 26 - tw), 7, y + 2);
                     c.setFont(&ui::FONT);

                     c.setTextColor(ui::ink(s, ui::C_TEXT));
                     c.drawString(ui::fitLine(m.message, w - 14), 7, y + 2 + ui::LINE_H);
                 });
}

static bool key(const Keys& k) {
    if (k.back) {
        core::ntfyVisible = false;
        return false;
    }
    if (k.has('m')) {  // Ton an/aus
        core::settings.muted = !core::settings.muted;
        core::saveSettings();
    }
    if (core::messages.empty()) return true;
    int i = selectedIndex();
    if (k.up) select(i - 1);
    else if (k.down) select(i + 1);
    else if (k.left) select(i - 3);
    else if (k.right) select(i + 3);
    else if (k.enter) {
        auto& m = core::messages[i];
        m.seen = true;
        String title = (m.title.isEmpty() ? m.topic : m.title) + "  " + text::fmtTime(m.time);
        String body = m.message;
        if (!m.title.isEmpty()) body += "\n\n[" + m.topic + "]";
        ui::viewText(title, body, ui::prioColor(m.priority));
    } else if (k.has('x')) {
        core::messages.erase(core::messages.begin() + i);
        select(i);
    } else if (k.has('a')) {  // alle als gelesen markieren
        for (auto& m : core::messages) m.seen = true;
    }
    return true;
}

static String badge() {
    int n = core::unread();
    if (n) return String(n) + " neu";
    return core::messages.empty() ? String() : String(core::messages.size());
}

App ntfyApp = {"Meldungen", enter, key, draw, nullptr, badge};
