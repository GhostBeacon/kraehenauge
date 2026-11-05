// Grove-Zubehoer: Unit OLED oder Unit Hex als Dauer-Anzeige neben dem Cardputer.
// Ansteuerung ueber lib/beiwerk (Kopie aus Takt). Immer nur ein Geraet am Grove-Port.
//
//   Hex, "Lampe":  Mitte = Meldungen/Verbindung, innerer Ring = Server-Temperatur,
//                  aeussere Ringe = Dienste der Statusseite (je Dienst eine LED)
//   Hex, "Ruhig":  dunkel, nur bei Problemen (Dienst aus, ungelesene Meldung, Warnung)
//   OLED:          System / Status / Meldung, oder "Wechsel" (alle 8 s die naechste)
//
// Tasten ueberall: Fn+E erkennen, Fn+R Ansicht, Fn+T Hex-Helligkeit bzw. OLED drehen.
// Auch unter Einstellungen -> Zubehoer.
#pragma once

#include <Arduino.h>

#include "keys.h"

namespace zub {

void begin();                 // nach core::begin(): Einstellungen laden, Geraet erkennen
void tick();                  // aus loop(): Hintergrundabfragen, Hex ~30/s, OLED bei Aenderung
bool taste(const Keys& k);    // Fn+E/R/T; true = verbraucht

String geraetName();          // "OLED", "Hex"
String ansichtName();
const char* erkennen();       // Rueckgabe: Meldung
const char* naechsteAnsicht();
const char* helligkeitOderDrehen();

}  // namespace zub
