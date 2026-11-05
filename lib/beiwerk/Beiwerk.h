// Beiwerk – Grove-Zubehoer fuer den M5Stack Cardputer ADV
//
// Erkennt, was am Grove-Port steckt, und steuert es an:
//   - Unit OLED (SH1107, 128x64, I2C 0x3C) ueber M5GFX
//   - Unit Hex (37 x SK6812) ueber den RMT-Baustein, Daten auf der gelben Leitung (G2)
// Es ist immer nur eines der beiden Geraete angeschlossen.
//
// Erkannt wird nur auf Anfrage (beim Start und per Taste), nicht laufend: Ein I2C-Scan
// wuerde am Hex als wirres Flackern ankommen.
#pragma once

#include <Arduino.h>
#include <M5UnitOLED.h>

namespace beiwerk {

enum class Geraet : uint8_t { Keins, Oled, Hex };

// Grove-Port des Cardputer ADV: G2 = SDA (gelb), G1 = SCL (weiss)
constexpr uint8_t PIN_GELB = 2;
constexpr uint8_t PIN_WEISS = 1;

// Prueft den Grove-Port und richtet das gefundene Geraet ein. Antwortet etwas auf 0x3C,
// ist es das OLED, sonst wird das Hex angenommen (es kann nicht antworten).
Geraet erkennen();
Geraet erzwingeHex();            // ohne I2C-Scan direkt das Hex ansteuern (Diagnose)
void trennen();                  // aktives Geraet ausschalten und Pins freigeben (Hex dunkel, OLED leer)
Geraet geraet();
const char* name(Geraet g);

// ---------------------------------------------------------------- OLED
// 128 x 64 im Querformat. Nur gueltig, solange geraet() == Geraet::Oled.
M5UnitOLED& oled();
void oledDrehen(bool gedreht);   // Anzeige um 180 Grad drehen (Kabel oben oder unten)

// ---------------------------------------------------------------- Hex
namespace hex {

constexpr int N = 37;            // Reihen 4-5-6-7-6-5-4, jede Reihe links nach rechts, 0 = oben links
constexpr int RINGE = 4;         // Ring 0 = Mitte (1 LED), dann 6, 12, 18 LEDs

// Helligkeit in Prozent, hart begrenzt auf 40 %: 37 LEDs ziehen voll weiss bis ~2 A.
void helligkeit(int prozent);
uint8_t helligkeit();

void clear();
void set(int i, uint32_t rgb);   // Farbe 0xRRGGBB (volle Skala, Helligkeit wird beim Senden angewandt)
void add(int i, uint32_t rgb);   // aufaddieren, je Kanal bei 255 gekappt
void fill(uint32_t rgb);
bool show();                     // sendet das Bild (~1,2 ms), false wenn kein Hex aktiv
void pin(uint8_t gpio);          // Datenleitung (Standard PIN_GELB); wirkt ab dem naechsten erkennen()
uint8_t pin();

int ringLaenge(int ring);        // 1, 6, 12, 18
int ring(int ring, int k);       // LED-Index: k-te LED des Rings, im Uhrzeigersinn ab oben
void ringFill(int ring, uint32_t rgb, bool addieren = false);

}  // namespace hex

// ---------------------------------------------------------------- Farben
uint32_t skaliere(uint32_t rgb, uint8_t faktor);   // faktor 0..255
uint32_t mische(uint32_t a, uint32_t b);           // Kanalweise addieren, gekappt

}  // namespace beiwerk
