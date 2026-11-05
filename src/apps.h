// Die einzelnen Bildschirme ("Apps"), erreichbar ueber das Startmenue.
#pragma once

#include <Arduino.h>

#include "keys.h"

struct App {
    const char* name;
    void (*enter)();                // beim Oeffnen aus dem Menue
    bool (*key)(const Keys& k);     // false = zurueck ins Menue
    void (*draw)();                 // Bildschirm neu zeichnen (ohne push)
    void (*tick)();                 // regelmaessig, solange offen (darf nullptr sein)
    String (*badge)();              // kurzer Zustand fuer das Menue
    void (*leave)();                // beim Zurueckgehen ins Menue (darf nullptr sein)
};

extern App homeApp;
extern App ntfyApp;
extern App statusApp;
extern App settingsApp;
extern App systemApp;

// Kompakte Sicht auf die Daten fuer das Grove-Zubehoer (zubehoer.cpp). hintergrund() fragt
// auch ab, wenn die jeweilige Funktion nicht offen ist - nur aufrufen, solange es gebraucht wird.
namespace statusdaten {
bool eingerichtet();
int anzahl();
int zustand(int i);   // 0 ausgefallen, 1 laeuft, 2 ausstehend, 3 Wartung, -1 keine Daten
String name(int i);
void hintergrund();   // alle 60 s (die erste Abfrage kurz nach dem Verbinden)
}  // namespace statusdaten

namespace systemdaten {
struct Kurz {
    bool gueltig = false;
    float cpu = -1, temp = -1, last = -1;
    int ramUsed = 0, ramTotal = 0;   // MB
    bool unterspannung = false, gedrosselt = false;
};
bool bereit();        // Server eingerichtet und angemeldet
Kurz kurz();
void hintergrund();   // alle 15 s; ist "System" offen, fragt die Funktion selbst oefter ab
}  // namespace systemdaten

