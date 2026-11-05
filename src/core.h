// Gemeinsamer Zustand und Hintergrundarbeit: WLAN, ntfy-Stream, Meldungen, Einstellungen,
// Abdunkeln. tick() muss oft laufen - auch waehrend Dialogen.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <deque>

#include "config.h"
#include "ntfy.h"
#include "quelle.h"

namespace core {

struct Settings {
    bool muted = false;
    uint8_t brightness = 128;    // 0..255
    uint32_t dimAfterMs = 60000; // 0 = nie
};

extern Config cfg;
extern Settings settings;
extern Preferences prefs;
extern std::deque<NtfyMessage> messages;  // neueste vorn, max. 30

void begin();  // nach loadConfig()
void tick();
void saveSettings();
void applyBrightness();

bool timeValid();
bool online();
String wifiSsid();
int wifiRssi();
String wifiDiagnosis();  // letzte WLAN-Ereignisse (Scan, Verbinden, Trennungsgrund), mehrzeilig

int unread();

// Meldungsquelle: Meldestelle (wenn server_url gesetzt) oder ntfy, siehe config.h
Quelle& quelle();
bool live();   // Live-Verbindung zur Quelle steht

// Nutzeraktivitaet. true = Display war abgedunkelt (Tastendruck nur zum Wecken).
bool wake();
void beep(uint8_t priority);

// Hintergrund hat etwas Sichtbares geaendert (neue Meldung, Netz, Minute, Hinweis).
void markDirty();
bool takeDirty();

// Kurzer Hinweis unten am Bildschirm (z. B. neue Meldung, waehrend eine andere App offen ist)
void toast(const String& text, uint32_t ms = 4000);
String currentToast();

// Ist die Meldungs-App gerade sichtbar? (dann kein Hinweis-Banner)
extern bool ntfyVisible;

// Display nicht abdunkeln (z. B. System-Monitor als Dauer-Anzeige)
extern bool keepAwake;

}  // namespace core
