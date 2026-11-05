// Krähenauge (Repo: raveneye) - Firmware fuer den M5Stack Cardputer ADV.
//
// Monitoring: Startmenue mit Meldungen (ntfy), Status (Uptime Kuma), System (Live-Werte
// eines Servers) und Einstellungen. Nur lesend - das Geraet aendert nichts auf den Servern. WLAN und ntfy laufen im Hintergrund weiter, egal welche App offen ist.
//
// Tasten ueberall:  ; . , /  = hoch runter links rechts     Enter = oeffnen
//                   ` (Esc) oder Del = zurueck               1-4 im Menue = direkt oeffnen

#include <M5Cardputer.h>

#include "apps.h"
#include "auth.h"
#include "boot.h"
#include "config.h"
#include "core.h"
#include "keys.h"
#include "ui.h"
#include "zubehoer.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// HTTPS + JSON + Dialoge brauchen mehr als die ueblichen 8 KB
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static App* current = &homeApp;
static bool dirty = true;

App* openApp(App* app) {
    current = app;
    if (app->enter) app->enter();
    dirty = true;
    return app;
}

static void fatal(const String& msg) {
    auto& d = M5Cardputer.Display;
    d.fillScreen(ui::C_BG);
    d.setFont(&ui::FONT_BOLD);
    d.setTextColor(ui::C_MAGENTA);
    d.drawString("KRÄHENAUGE // FEHLER", 4, 4);
    d.drawFastHLine(0, 18, d.width(), ui::C_ERR);
    d.setFont(&ui::FONT);
    d.setTextColor(ui::C_BRIGHT);
    d.drawString(msg, 4, 26);
    d.setTextColor(ui::C_HINT);
    d.drawString("SD: /raveneye/config.txt", 4, 60);
    while (true) delay(1000);
}

void setup() {
    auto m5cfg = M5.config();
    M5Cardputer.begin(m5cfg, true);
    Serial.begin(115200);

    auto& d = M5Cardputer.Display;
    d.setRotation(1);
    d.setBrightness(128);
    d.fillScreen(ui::C_BG);

    String err;
    if (!loadConfig(core::cfg, err)) fatal(err);

    ui::begin();
    core::begin();
    auth::begin();
    zub::begin();   // OLED oder Hex am Grove-Port
    boot::run(FW_VERSION);
}

void loop() {
    Keys k;
    bool pressed = keys::poll(k);
    core::tick();

    if (pressed && zub::taste(k)) {   // Fn+E/R/T: Zubehoer, in jeder Funktion
        dirty = true;
    } else if (pressed) {
        App* before = current;
        bool stay = current->key(k);
        if (!stay && current == before && current != &homeApp) {
            if (current->leave) current->leave();
            current = &homeApp;
        }
        dirty = true;
    }
    if (current->tick) current->tick();
    zub::tick();

    if (core::takeDirty()) dirty = true;
    if (dirty) {
        current->draw();
        ui::push();
        dirty = false;
    }
    delay(5);
}
