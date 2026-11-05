// Beiwerk – Grove-Zubehoer fuer den M5Stack Cardputer ADV (siehe Beiwerk.h)
#include "Beiwerk.h"

#include <driver/rmt.h>
#include <math.h>

namespace beiwerk {

// I2C-Port 0 ist am Cardputer ADV frei: M5Unified legt Tastatur und Audio-Codec auf Port 1.
static constexpr int I2C_PORT = 0;
static constexpr uint8_t OLED_ADDR = 0x3C;

static Geraet aktiv = Geraet::Keins;

// ---------------------------------------------------------------- OLED
static M5UnitOLED oledObj(PIN_GELB, PIN_WEISS, 400000, I2C_PORT, OLED_ADDR);
static bool oledBereit = false;   // init() lief schon einmal (M5GFX initialisiert danach nicht erneut)
static bool oledGedreht = false;

M5UnitOLED& oled() { return oledObj; }

static void oledRotation() { oledObj.setRotation(oledGedreht ? 3 : 1); }

void oledDrehen(bool gedreht) {
  oledGedreht = gedreht;
  if (aktiv == Geraet::Oled) {
    oledRotation();
    oledObj.clear();
  }
}

static bool oledStart() {
  bool ok;
  if (!oledBereit) ok = oledBereit = oledObj.init();
  else ok = oledObj.getPanel()->init(false);   // nach einem Wechsel zum Hex: Bus und Controller neu aufsetzen
  if (!ok) return false;
  oledRotation();
  oledObj.clear();
  return true;
}

static void oledStop() {
  oledObj.clear();
  oledObj.getPanel()->getBus()->release();
}

// ---------------------------------------------------------------- Hex (SK6812 ueber RMT)
namespace hex {

static constexpr rmt_channel_t KANAL = RMT_CHANNEL_3;   // S3: Sendekanaele 0-3, die Arduino-HAL vergibt von vorn
static uint8_t bild[N][3];                              // R, G, B in voller Skala
static rmt_item32_t items[N * 24];
static uint8_t hell = 10;
static bool an = false;
static uint8_t datenPin = PIN_GELB;

void pin(uint8_t gpio) { datenPin = gpio; }
uint8_t pin() { return datenPin; }

// 40 MHz (clk_div 2) -> 25 ns pro Tick. SK6812: 0 = 0,3/0,9 us, 1 = 0,6/0,6 us
static constexpr rmt_item32_t BIT_NULL = {{{12, 1, 36, 0}}};
static constexpr rmt_item32_t BIT_EINS = {{{24, 1, 24, 0}}};

static bool start() {
  rmt_config_t c = RMT_DEFAULT_CONFIG_TX((gpio_num_t)datenPin, KANAL);
  c.clk_div = 2;
  if (rmt_config(&c) != ESP_OK) return false;
  if (rmt_driver_install(KANAL, 0, 0) != ESP_OK) return false;
  an = true;
  return true;
}

static void stop() {
  if (!an) return;
  rmt_driver_uninstall(KANAL);
  gpio_reset_pin((gpio_num_t)datenPin);
  an = false;
}

void helligkeit(int prozent) { hell = prozent > 40 ? 40 : (prozent < 1 ? 1 : prozent); }
uint8_t helligkeit() { return hell; }

void clear() { memset(bild, 0, sizeof(bild)); }

void set(int i, uint32_t rgb) {
  if (i < 0 || i >= N) return;
  bild[i][0] = rgb >> 16;
  bild[i][1] = rgb >> 8;
  bild[i][2] = rgb;
}

void add(int i, uint32_t rgb) {
  if (i < 0 || i >= N) return;
  uint32_t alt = ((uint32_t)bild[i][0] << 16) | ((uint32_t)bild[i][1] << 8) | bild[i][2];
  set(i, mische(alt, rgb));
}

void fill(uint32_t rgb) {
  for (int i = 0; i < N; i++) set(i, rgb);
}

bool show() {
  if (!an) return false;
  uint32_t f = (uint32_t)hell * 255 / 100;   // Prozent -> Faktor 0..102
  rmt_item32_t* it = items;
  for (int i = 0; i < N; i++) {
    const uint8_t grb[3] = {bild[i][1], bild[i][0], bild[i][2]};   // SK6812 erwartet G, R, B
    for (uint8_t v : grb) {
      uint8_t s = (v * f + 254) / 255;   // aufrunden: schwache Farben gehen nicht ganz verloren
      for (int b = 7; b >= 0; b--) *it++ = (s >> b) & 1 ? BIT_EINS : BIT_NULL;
    }
  }
  return rmt_write_items(KANAL, items, N * 24, true) == ESP_OK;
}

// Ringe: Abstand zur Mitte im Sechseckgitter, sortiert im Uhrzeigersinn ab oben
static const int REIHE[7] = {4, 5, 6, 7, 6, 5, 4};
static const int RING_LEN[RINGE] = {1, 6, 12, 18};
static int8_t ringIdx[RINGE][18];
static bool ringeFertig = false;

static void ringeBauen() {
  float winkel[RINGE][18];
  int n[RINGE] = {0};
  int i = 0;
  for (int r = 0; r < 7; r++) {
    int rr = r - 3;                          // axiale Koordinaten: rr Reihe, q Spalte
    int qmin = rr < 0 ? -3 - rr : -3;
    for (int c = 0; c < REIHE[r]; c++, i++) {
      int q = qmin + c;
      int d = (abs(q) + abs(rr) + abs(q + rr)) / 2;
      float x = q + rr * 0.5f, y = rr * 0.8660254f;
      float w = atan2f(x, -y);               // 0 = oben, positiv im Uhrzeigersinn (y zeigt nach unten)
      if (w < -1e-4f) w += 2 * PI;
      int k = n[d]++;
      ringIdx[d][k] = i;
      winkel[d][k] = w;
    }
  }
  for (int d = 1; d < RINGE; d++)            // Einfuegesortierung nach Winkel
    for (int a = 1; a < n[d]; a++)
      for (int b = a; b > 0 && winkel[d][b] < winkel[d][b - 1]; b--) {
        std::swap(winkel[d][b], winkel[d][b - 1]);
        std::swap(ringIdx[d][b], ringIdx[d][b - 1]);
      }
  ringeFertig = true;
}

int ringLaenge(int r) { return (r >= 0 && r < RINGE) ? RING_LEN[r] : 0; }

int ring(int r, int k) {
  if (r < 0 || r >= RINGE) return -1;
  if (!ringeFertig) ringeBauen();
  int len = RING_LEN[r];
  k %= len;
  if (k < 0) k += len;
  return ringIdx[r][k];
}

void ringFill(int r, uint32_t rgb, bool addieren) {
  for (int k = 0; k < ringLaenge(r); k++) {
    if (addieren) add(ring(r, k), rgb);
    else set(ring(r, k), rgb);
  }
}

}  // namespace hex

// ---------------------------------------------------------------- Erkennung
static bool oledAntwortet() {
  if (!lgfx::i2c::init(I2C_PORT, PIN_GELB, PIN_WEISS).has_value()) return false;
  bool da = false;
  for (int v = 0; v < 3 && !da; v++) {
    bool ack = lgfx::i2c::beginTransaction(I2C_PORT, OLED_ADDR, 100000, false).has_value();
    da = lgfx::i2c::endTransaction(I2C_PORT).has_value() && ack;
    if (!da) delay(2);
  }
  lgfx::i2c::release(I2C_PORT);
  return da;
}

void trennen() {
  if (aktiv == Geraet::Hex) {
    hex::clear();
    hex::show();
    hex::stop();
  } else if (aktiv == Geraet::Oled) {
    oledStop();
  }
  aktiv = Geraet::Keins;
}

Geraet erzwingeHex() {
  trennen();
  if (hex::start()) {
    aktiv = Geraet::Hex;
    hex::clear();
    hex::show();
  }
  return aktiv;
}

Geraet erkennen() {
  trennen();
  if (oledAntwortet()) {
    if (oledStart()) aktiv = Geraet::Oled;
  } else if (hex::start()) {
    aktiv = Geraet::Hex;
    hex::clear();
    hex::show();   // Reste vom I2C-Scan auf der Datenleitung ueberschreiben
  }
  return aktiv;
}

Geraet geraet() { return aktiv; }

const char* name(Geraet g) {
  switch (g) {
    case Geraet::Oled: return "OLED";
    case Geraet::Hex: return "Hex";
    default: return "kein Gerät";
  }
}

// ---------------------------------------------------------------- Farben
uint32_t skaliere(uint32_t rgb, uint8_t f) {
  uint32_t r = ((rgb >> 16) & 255) * f / 255, g = ((rgb >> 8) & 255) * f / 255, b = (rgb & 255) * f / 255;
  return (r << 16) | (g << 8) | b;
}

uint32_t mische(uint32_t a, uint32_t b) {
  uint32_t r = ((a >> 16) & 255) + ((b >> 16) & 255);
  uint32_t g = ((a >> 8) & 255) + ((b >> 8) & 255);
  uint32_t bl = (a & 255) + (b & 255);
  return ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (bl > 255 ? 255 : bl);
}

}  // namespace beiwerk
