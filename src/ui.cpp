#include "ui.h"

#include <WiFi.h>
#include <time.h>

#include "core.h"
#include "fonts.h"
#include "keys.h"
#include "text.h"

namespace ui {

M5Canvas canvas(&M5Cardputer.Display);
const lgfx::U8g2font FONT(raveneye_spleen6x12);
const lgfx::U8g2font FONT_BOLD(raveneye_spleen6x12b);
int LINE_H = 14;
int HEADER_H = 16;

void begin() {
    canvas.setColorDepth(16);
    canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    canvas.setFont(&FONT);
    canvas.setTextDatum(top_left);
    LINE_H = canvas.fontHeight() + 2;  // 2 px Luft zwischen den Zeilen
    HEADER_H = LINE_H + 2;
}

int width() { return canvas.width(); }
int height() { return canvas.height(); }

// ---------- Text ----------

String fitLine(const String& s, int maxW) {
    String one = s;
    int nl = one.indexOf('\n');
    if (nl >= 0) one = one.substring(0, nl);
    if (canvas.textWidth(one) <= maxW) return one;
    String out;
    for (int i = 0; i < (int)one.length();) {
        int l = text::utf8Len(one, i);
        String next = out + one.substring(i, i + l);
        if (canvas.textWidth(next + "...") > maxW) break;
        out = next;
        i += l;
    }
    return out + "...";
}

std::vector<String> wrap(const String& txt, int maxW) {
    std::vector<String> lines;
    int start = 0;
    while (start <= (int)txt.length()) {
        int nl = txt.indexOf('\n', start);
        String para = txt.substring(start, nl < 0 ? txt.length() : nl);
        start = (nl < 0) ? txt.length() + 1 : nl + 1;

        String line;
        int i = 0;
        while (i < (int)para.length()) {
            int sp = para.indexOf(' ', i);
            String word = para.substring(i, sp < 0 ? para.length() : sp);
            i = (sp < 0) ? para.length() : sp + 1;
            String cand = line.isEmpty() ? word : line + " " + word;
            if (canvas.textWidth(cand) <= maxW) {
                line = cand;
                continue;
            }
            if (!line.isEmpty()) lines.push_back(line);
            line = "";
            for (int j = 0; j < (int)word.length();) {  // ueberlanges Wort zeichenweise trennen
                int l = text::utf8Len(word, j);
                String c = word.substring(j, j + l);
                if (canvas.textWidth(line + c) > maxW) {
                    lines.push_back(line);
                    line = "";
                }
                line += c;
                j += l;
            }
        }
        lines.push_back(line);
    }
    return lines;
}

uint16_t prioColor(uint8_t p) {
    switch (p) {
        case 5: return C_ERR;
        case 4: return C_WARN;
        case 1:
        case 2: return C_HINT;
        default: return C_BRIGHT;
    }
}

// ---------- Rahmen ----------

// Grossbuchstaben fuer ASCII und Latin-1 (ä -> Ä ...), ß bleibt
static String upper(const String& in) {
    String s = in;
    for (int i = 0; i < (int)s.length(); i++) {
        uint8_t c = s[i];
        if (c >= 'a' && c <= 'z') s.setCharAt(i, c - 32);
        else if (c == 0xC3 && i + 1 < (int)s.length()) {
            uint8_t d = s[i + 1];
            if (d >= 0xA0 && d <= 0xBE && d != 0xB7) s.setCharAt(i + 1, d - 0x20);
            i++;
        }
    }
    return s;
}

// Titelzeile: schwarz, Titel in Magenta, cyane Linie darunter
static void headerBar(const String& title, uint16_t color, int maxW) {
    canvas.fillRect(0, 0, canvas.width(), HEADER_H, C_BG);
    canvas.drawFastHLine(0, HEADER_H - 2, canvas.width(), C_CYAN);
    canvas.setFont(&FONT_BOLD);
    canvas.setTextColor(color);
    canvas.drawString(fitLine(upper(title), maxW), 4, 1);
    canvas.setFont(&FONT);
}

void clear() {
    canvas.fillScreen(C_BG);
    canvas.setFont(&FONT);
}

void drawHeader(const char* title) {
    canvas.fillRect(0, 0, canvas.width(), HEADER_H, C_BG);
    int x = canvas.width() - 4;
    char buf[16];
    snprintf(buf, sizeof buf, "%d%%", M5Cardputer.Power.getBatteryLevel());
    canvas.setTextColor(C_TEXT);
    x -= canvas.textWidth(buf);
    canvas.drawString(buf, x, 1);

    if (core::timeValid()) {
        time_t n = time(nullptr);
        struct tm m;
        localtime_r(&n, &m);
        strftime(buf, sizeof buf, "%H:%M", &m);
        x -= canvas.textWidth(buf) + 6;
        canvas.drawString(buf, x, 1);
    }

    // Verbindungspunkt: gruen = Live-Verbindung zur Meldungsquelle, gelb = nur WLAN, rot = kein WLAN
    uint16_t dot = C_ERR;
    if (core::online()) dot = core::live() ? C_OK : C_YELLOW;
    x -= 8;
    canvas.fillRect(x - 2, HEADER_H / 2 - 3, 4, 4, dot);
    x -= 6;

    int n = core::unread();
    if (n > 0) {
        String s = String("• ") + n;
        x -= canvas.textWidth(s) + 2;
        canvas.setTextColor(C_MAGENTA);
        canvas.drawString(s, x, 1);
    }
    if (core::settings.muted) {
        x -= canvas.textWidth("stumm") + 6;
        canvas.setTextColor(C_HINT);
        canvas.drawString("stumm", x, 1);
    }

    String t = upper(title);
    canvas.setFont(&FONT_BOLD);
    canvas.setTextColor(C_MAGENTA);
    canvas.drawString(fitLine(t, x - 10), 4, 1);
    canvas.setFont(&FONT);
    canvas.drawFastHLine(0, HEADER_H - 2, canvas.width(), C_CYAN);
}

void drawHint(const String& hint) {
    int y = canvas.height() - LINE_H;
    canvas.fillRect(0, y, canvas.width(), LINE_H, C_BG);
    canvas.drawFastHLine(0, y, canvas.width(), C_DIM);
    canvas.setTextColor(C_HINT);
    canvas.drawString(fitLine(hint, canvas.width() - 8), 4, y + 2);
}

int contentTop() { return HEADER_H + 1; }
int contentBottom(bool withHint) { return canvas.height() - (withHint ? LINE_H : 0); }

void push() {
    String t = core::currentToast();
    if (!t.isEmpty()) {
        int h = LINE_H + 4;
        int y = canvas.height() - h;
        canvas.fillRect(0, y, canvas.width(), h, C_BG);
        canvas.drawFastHLine(0, y, canvas.width(), C_MAGENTA);
        canvas.fillRect(0, y, 3, h, C_MAGENTA);
        canvas.setTextColor(C_BRIGHT);
        canvas.drawString(fitLine(t, canvas.width() - 8), 4, y + 2);
    }
    canvas.pushSprite(0, 0);
}

void drawList(int count, int sel, int& top, int y0, int y1, int rowH,
              const std::function<void(int, int, bool)>& row) {
    int rows = max(1, (y1 - y0) / rowH);
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    if (top > max(0, count - rows)) top = max(0, count - rows);
    if (top < 0) top = 0;
    for (int r = 0; r < rows && top + r < count; r++) {
        int y = y0 + r * rowH;
        bool s = (top + r) == sel;
        if (s) {  // Auswahl: cyaner Balken mit magentafarbener Kante links
            canvas.fillRect(0, y, canvas.width(), rowH, C_SELECT);
            canvas.fillRect(0, y, 3, rowH, C_MAGENTA);
        }
        row(top + r, y, s);
    }
    if (count > rows) {  // Scrollbalken
        int h = y1 - y0;
        int barH = max(6, h * rows / count);
        int barY = y0 + (h - barH) * top / max(1, count - rows);
        canvas.fillRect(canvas.width() - 2, y0, 2, h, C_DIM);
        canvas.fillRect(canvas.width() - 2, barY, 2, barH, C_CYAN);
    }
}

// ---------- Dialoge ----------

// Wartet auf das naechste Tastenereignis und haelt dabei den Hintergrund am Laufen.
// redraw() wird bei Hintergrund-Aenderungen (Uhrzeit, Banner ...) erneut aufgerufen.
static Keys waitKey(const std::function<void()>& redraw) {
    Keys k;
    redraw();
    while (true) {
        core::tick();
        if (keys::poll(k)) return k;
        if (core::takeDirty()) redraw();
        delay(5);
    }
}

void message(const String& title, const String& body, uint16_t titleColor) {
    viewText(title, body, titleColor);
}

bool confirm(const String& title, const String& question) {
    auto lines = wrap(question, canvas.width() - 8);
    auto draw = [&]() {
        clear();
        drawHeader(title.c_str());
        canvas.setTextColor(C_BRIGHT);
        int y = contentTop() + 4;
        for (auto& l : lines) {
            canvas.drawString(l, 4, y);
            y += LINE_H;
        }
        drawHint("Enter: Ja    ` / Del: Nein");
        push();
    };
    while (true) {
        Keys k = waitKey(draw);
        if (k.enter) return true;
        if (k.back || k.has('n')) return false;
        if (k.has('j') || k.has('y')) return true;
    }
}

int choose(const String& title, const std::vector<String>& options, int initial) {
    int sel = constrain(initial, 0, (int)options.size() - 1), top = 0;
    auto draw = [&]() {
        clear();
        drawHeader(title.c_str());
        drawList(options.size(), sel, top, contentTop(), contentBottom(false), LINE_H + 2,
                 [&](int i, int y, bool s) {
                     canvas.setTextColor(ink(s, C_BRIGHT));
                     canvas.drawString(fitLine(options[i], canvas.width() - 12), 7, y + 1);
                 });
        push();
    };
    while (true) {
        Keys k = waitKey(draw);
        if (k.up && sel > 0) sel--;
        else if (k.down && sel + 1 < (int)options.size()) sel++;
        else if (k.enter) return sel;
        else if (k.back) return -1;
    }
}

bool textInput(const String& prompt, String& value, bool secret, size_t maxLen) {
    String v = value;
    bool show = false;
    auto draw = [&]() {
        clear();
        drawHeader(prompt.c_str());
        int boxY = contentTop() + 8, boxH = LINE_H + 8, w = canvas.width();
        canvas.drawRect(2, boxY, w - 4, boxH, C_CYAN);
        canvas.fillRect(2, boxY, 3, boxH, C_MAGENTA);
        String shown;
        if (secret && !show) {
            for (int i = 0; i < (int)v.length(); i += text::utf8Len(v, i)) shown += "•";
        } else {
            shown = v;
        }
        shown += "_";
        // Nur das Ende zeigen, wenn der Text zu lang ist
        while (canvas.textWidth(shown) > w - 14 && shown.length() > 1) shown.remove(0, text::utf8Len(shown, 0));
        canvas.setTextColor(C_BRIGHT);
        canvas.drawString(shown, 9, boxY + 4);
        canvas.setTextColor(C_HINT);
        int y = boxY + boxH + 6;
        canvas.drawString("Enter: OK    Fn + `: Abbrechen", 4, y);
        if (secret) canvas.drawString("Tab: Eingabe zeigen/verbergen", 4, y + LINE_H);
        push();
    };
    while (true) {
        Keys k = waitKey(draw);
        if (k.fn && k.esc) return false;
        if (k.enter) {
            value = v;
            return true;
        }
        if (k.tab) {
            show = !show;
            continue;
        }
        if (k.del) {
            if (v.length()) {
                int i = 0, last = 0;  // letztes UTF-8-Zeichen entfernen
                while (i < (int)v.length()) {
                    last = i;
                    i += text::utf8Len(v, i);
                }
                v.remove(last);
            }
            continue;
        }
        if (k.fn) continue;  // Fn + Taste = Navigation, nicht tippen
        for (char c : k.chars)
            if (v.length() < maxLen && c >= 0x20 && c < 0x7F) v += c;
    }
}

void viewText(const String& title, const String& body, uint16_t titleColor) {
    const int w = canvas.width() - 8;
    auto lines = wrap(body, w);
    int scroll = 0;
    const int y0 = contentTop() + 1;
    const int maxLines = (canvas.height() - y0) / LINE_H;
    int maxScroll = max(0, (int)lines.size() - maxLines);
    auto draw = [&]() {
        clear();
        headerBar(title, titleColor, canvas.width() - 8);
        canvas.setTextColor(C_BRIGHT);
        for (int i = 0; i < maxLines && scroll + i < (int)lines.size(); i++)
            canvas.drawString(lines[scroll + i], 4, y0 + i * LINE_H);
        if (maxScroll > 0) {
            int h = canvas.height() - y0;
            int barH = max(8, h * maxLines / (int)lines.size());
            int barY = y0 + (h - barH) * scroll / maxScroll;
            canvas.fillRect(canvas.width() - 2, barY, 2, barH, C_CYAN);
        }
        push();
    };
    while (true) {
        Keys k = waitKey(draw);
        if (k.up && scroll > 0) scroll--;
        else if (k.down && scroll < maxScroll) scroll++;
        else if (k.left) scroll = max(0, scroll - maxLines);
        else if (k.right) scroll = min(maxScroll, scroll + maxLines);
        else if (k.back || k.enter) return;
    }
}

void busy(const String& title, const String& txt) {
    clear();
    drawHeader(title.c_str());
    canvas.setTextColor(C_TEXT);
    int y = contentTop() + 4;
    for (auto& l : wrap(txt, canvas.width() - 8)) {
        canvas.drawString(l, 4, y);
        y += LINE_H;
    }
    push();
}

void progress(const String& title, const String& txt, uint64_t done, uint64_t total) {
    clear();
    drawHeader(title.c_str());
    canvas.setTextColor(C_BRIGHT);
    int y = contentTop() + 4;
    canvas.drawString(fitLine(txt, canvas.width() - 8), 4, y);
    y += LINE_H + 8;
    int w = canvas.width() - 8;
    canvas.drawRect(4, y, w, 12, C_CYAN);
    if (total > 0) {  // Segmente wie auf dem Startbild
        int fill = (int)((w - 4) * min(done, total) / total);
        for (int x = 0; x < fill; x += 4) canvas.fillRect(6 + x, y + 2, min(3, fill - x), 8, C_CYAN);
    }
    y += 18;
    canvas.setTextColor(C_TEXT);
    String s = text::fmtBytes(done);
    if (total > 0) s += " von " + text::fmtBytes(total);
    canvas.drawString(s, 4, y);
    push();
}

}  // namespace ui
