<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/banner/banner-dunkel.png">
  <img alt="KRÄHENAUGE – Monitoring für den Cardputer ADV" src="docs/banner/banner-hell.png">
</picture>

# Krähenauge 🐦‍⬛

Teil von **CORVUS** (Repo corvus, privat) – sichtbarer Name **Krähenauge**; Repo, Firmware-Datei (`raveneye.bin`) und Release-Links heißen weiter `raveneye`, damit der Launcher-Favorit unverändert funktioniert. Beim Anmelden am Server erscheint das Gerät als „Krähenauge (Cardputer)“ (bis Version 1.0.20 angemeldete Geräte weiter als „RavenEye“).

Eigene Firmware für den **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB Flash, Tastatur-Chip TCA8418), gestartet über den [M5Launcher](https://github.com/bmorcelli/Launcher). Ein kleines Auge, das wach bleibt: reines **Monitoring** – es zeigt an und meldet, ändert aber nichts auf den Servern.

Aussehen „Neon Noir“: schwarz, Cyan und Magenta, Pixelschrift. Beim Einschalten erscheint ein kurzes Startbild (~2,6 s, jede Taste überspringt es), während WLAN und ntfy schon verbinden.

Ein Startmenü mit vier Funktionen. WLAN und ntfy laufen immer im Hintergrund weiter, auch wenn gerade eine andere Funktion offen ist – neue Meldungen erscheinen dann als Hinweis unten am Bildschirm.

| # | Funktion | Was sie tut |
|---|---|---|
| 1 | **Meldungen** | [ntfy](https://ntfy.sh)-Empfänger für ein oder mehrere Topics |
| 2 | **Status** | Dienste einer [Uptime-Kuma](https://github.com/louislam/uptime-kuma)-Statusseite, mit Verlauf |
| 3 | **System** | Live-Werte eines Servers (CPU, Temperatur, RAM, Netz, Platte, Strom, Backups, Plattenbelegung) – auch als Dauer-Anzeige |
| 4 | **Einstellungen** | Ton, Helligkeit, Abdunkeln, WLAN, Server-Anmeldung, Speicher, Neustart |

## Bedienung

| Taste | Wirkung |
|---|---|
| `;` `.` | hoch / runter (gedrückt halten = weiter) |
| `,` `/` | seitenweise zurück / vor |
| Enter | öffnen / auswählen |
| `` ` `` (Esc) oder Del | zurück – aus einer Funktion ins Menü |
| `1`–`4` | im Menü: Funktion direkt öffnen |
| `m` | im Menü und bei den Meldungen: Ton an/aus (bleibt gespeichert) |

**Texteingabe** (Anmeldung): Enter = OK, Del = Zeichen löschen, **Fn + `** = abbrechen, Tab = Passwort zeigen/verbergen.

Oben rechts: Punkt grün = ntfy-Stream läuft, gelb = WLAN ohne Stream, rot = kein WLAN; `• 3` = ungelesene Meldungen; dazu Uhrzeit und Akku. Die Version steht auf dem Startbildschirm (`1.0.<Build-Nr.>`, lokal gebaut: `dev`).

### Meldungen

- Beim Start werden die Meldungen der letzten 12 h geladen (Cache von ntfy.sh), danach kommen neue live. Nach einem Verbindungsabbruch holt `since=<letzte id>` Verpasstes nach.
- Ton bei neuen Meldungen, doppelt ab Priorität 4. Farbe nach Priorität (5 rot, 4 orange, 1–2 grau).
- Enter öffnet die Meldung, `x` entfernt sie vom Gerät, `a` markiert alle als gelesen.

### Status

- Liest die öffentliche Statusseite von Uptime Kuma 1.x (`/api/status-page/<slug>` und `/api/status-page/heartbeat/<slug>`), jede Minute neu, solange die Funktion offen ist; `r` lädt sofort.
- Pro Dienst: Status (grün läuft, rot ausgefallen, orange ausstehend, blau Wartung) und Verfügbarkeit der letzten 24 h. Enter zeigt die letzten Messungen als Balken (Uptime Kuma 1: 50, Kuma 2: 100 – die Balkenbreite passt sich an) und die Antwortzeit.
- Voraussetzung: In Uptime Kuma eine Statusseite anlegen und die gewünschten Monitore hinzufügen. **Die Seite ist öffentlich** – wer die Adresse kennt, sieht die Namen der Monitore.

### System

- Fragt alle 5 s `<server_url>/api/v1/system_stats` ab. Die Verbindung bleibt dabei offen (kein TLS-Handshake pro Abfrage) und wird beim Verlassen geschlossen.
- **Solange „System“ offen ist, dunkelt das Display nicht ab** – am Ladekabel als dauerhafte Anzeige neben dem Server.
- Vier Seiten, Wechsel mit `,` `/` oder Tab:
  1. **Live:** CPU (gesamt, Takt, Balken pro Kern), Temperatur, Last, RAM, Netz- und Plattendurchsatz (aus der Differenz zweier Abfragen), Stromverbrauch, Laufzeit. Unterspannung, Drosselung oder Temperaturlimit erscheinen als rote Zeile.
  2. **Verlauf:** CPU und Temperatur der letzten ~10 Minuten.
  3. **Backups & Energie:** letzter Lauf je Backup (grün ok, orange überfällig, rot fehlgeschlagen), Verbrauch im Monat und Jahr.
  4. **Speicher:** Belegung von HDD, Backup-HDD und SD-Karte (belegt / gesamt, Balken ab 80 % orange, ab 90 % rot). Eine nicht eingehängte Platte erscheint rot.
- Werte, die der Server nicht liefert, erscheinen als „-“. Der Server entscheidet, welche Konten die Werte sehen dürfen (sonst „nicht freigegeben“).
- **Anmeldung** beim ersten Öffnen (oder unter Einstellungen → Server) mit Benutzername, Passwort und ggf. 2FA-Code. Das Gerät fordert ein **Nur-Lese-Token** an (`scope: monitor`, Gerätename „Krähenauge“): Es darf nur Systemwerte lesen – keine Dateien, nichts löschen. Liefert der Server kein solches Token, wird es sofort wieder verworfen. Passwort und Code werden nicht gespeichert.

### Zubehör am Grove-Port

Am Grove-Port kann **eine** von zwei Erweiterungen stecken, als Dauer-Anzeige neben dem Cardputer:

- **Unit Hex** (37 RGB-LEDs), Ansicht **Lampe**: Die Mitte atmet in der Farbe der neuesten ungelesenen Meldung (5 rot, 4 orange, 3 cyan, 1–2 grau), sonst zeigt sie schwach die Verbindung (grün ntfy-Stream, gelb nur WLAN, rot offline). Der innere Ring zeigt die Server-Temperatur (40–80 °C auf 6 LEDs, grün/orange/rot); bei Unterspannung oder Drosselung blinkt er rot. Die beiden äußeren Ringe haben je Dienst der Statusseite eine LED (bis 30): grün läuft, rot blinkend ausgefallen, orange ausstehend, blau Wartung. Eine neue Meldung lässt einen Kometen außen kreisen. Ansicht **Ruhig**: dunkel, nur Probleme und ungelesene Meldungen leuchten.
- **Unit OLED** (SH1107, 128×64): **System** (CPU, Temperatur groß, RAM, Last, Warnungen), **Status** (Anzahl ok, ausgefallene Dienste), **Meldung** (neueste Meldung), **Wechsel** (alle 8 s die nächste).

| Taste | Funktion |
|---|---|
| Fn + `E` | Gerät neu erkennen (nach dem Umstecken) |
| Fn + `R` | Ansicht wechseln |
| Fn + `T` | Hex: Helligkeit (3 Stufen) · OLED: um 180° drehen |

Dasselbe unter **Einstellungen → Zubehör**. Erkannt wird beim Start: Antwortet ein OLED, wird es genutzt, sonst wird das Hex angesteuert. Ansicht, Helligkeit und Drehung bleiben gespeichert.

Damit die Anzeige aktuell bleibt, fragt Krähenauge Statusseite (alle 60 s) und Systemwerte (alle 15 s) **auch im Hintergrund** ab, solange die gewählte Ansicht sie braucht. Die Verbindung zum Server bleibt dabei offen (wie in „System“); das kostet etwas Arbeitsspeicher und Akku. Die Ansteuerung steckt in [`lib/beiwerk`](lib/beiwerk) (dieselben Dateien wie in [Takt](https://github.com/GhostBeacon/takt)).

### Schrift

[Spleen](https://github.com/fcambus/spleen) 6×12 (Pixel-Monospace, BSD-Lizenz) mit Umlauten, ß, °, ergänzt um selbst gezeichnete Zeichen € „ “ ” ‚ ‘ ’ – — • …; die fette Variante ist daraus abgeleitet. Emojis werden weggelassen, andere unbekannte Zeichen als `?` angezeigt.

## Konfiguration

Die Firmware enthält keine Zugangsdaten. Sie liest beim Start `/raveneye/config.txt` von der SD-Karte, Vorlage: [`sd-beispiel/raveneye/config.txt`](sd-beispiel/raveneye/config.txt).

```
wifi_ssid=<WLAN_NAME>
wifi_pass=<WLAN_PASSWORT>
wifi2_ssid=<WLAN2_NAME>        # optional, bis wifi9_*
wifi2_pass=<WLAN2_PASSWORT>
ntfy_server=ntfy.sh
ntfy_topics=<TOPIC>[,<TOPIC2>]
status_url=https://<STATUS_HOST>   # optional
status_slug=<SLUG>
server_url=https://<SERVER_HOST>   # optional, für „System“
```

- Pro Zeile `name=wert`. Zeilen, die mit `#` beginnen, sind Kommentare. Leerzeichen am Rand des Werts werden entfernt.
- Bis zu 9 WLANs (`wifi_*`, `wifi2_*` … `wifi9_*`). Verbunden wird mit dem stärksten, das gerade erreichbar ist; bei Abbruch wird alle 10 s neu gesucht.
- Die WLANs müssen 2,4 GHz können, der ESP32-S3 kann kein 5 GHz.
- Topics: nur `A–Z a–z 0–9 - _`, mehrere mit Komma ohne Leerzeichen.
- `status_url` und `server_url`: nur `https://`, ohne Pfad am Ende. Fehlen sie, zeigt die jeweilige Funktion „nicht eingerichtet“.

| Fehleranzeige | Ursache |
|---|---|
| `Keine SD-Karte` | Karte fehlt oder ist nicht FAT32 |
| `/raveneye/config.txt fehlt` | Ordner oder Dateiname falsch |
| `wifi_ssid fehlt` | kein einziges WLAN eingetragen oder Schlüssel falsch geschrieben |
| `ntfy_topics ungueltig` | Leerzeichen, spitze Klammern oder Sonderzeichen im Topic |
| `URL muss https:// sein` | `status_url` oder `server_url` beginnt nicht mit `https://` |

## Installieren

**Über WLAN mit dem M5Launcher:** Bei jedem Push auf `main` baut GitHub Actions die Firmware und legt ein Release an. Der Link auf die neueste Version bleibt immer gleich:

```
https://github.com/GhostBeacon/raveneye/releases/latest/download/raveneye.bin
```

Einmalig als Favorit eintragen: SD-Karte in den Rechner, `/config.conf` vorher sichern, dann in der Liste `"favorite": [ … ]` ergänzen (vom vorigen Eintrag mit Komma trennen):

```json
{
  "name": "Krähenauge",
  "fid": "",
  "link": "https://github.com/GhostBeacon/raveneye/releases/latest/download/raveneye.bin"
}
```

`fid` muss leer bleiben, sonst sucht der Launcher im M5Burner-Katalog statt über den Link. Danach im Launcher: **OTA → Favorite List → Krähenauge → Install**, künftige Updates genauso.

**Von der SD-Karte:** `raveneye.bin` aus einem [Release](https://github.com/GhostBeacon/raveneye/releases) (oder eine lokal gebaute `firmware.bin`) auf die Karte kopieren, im Launcher **SD** → Datei → **Install**.

Zum Launcher zurück: beim Einschalten eine Taste drücken, solange der Launcher-Startbildschirm zu sehen ist.

## Speicher und andere Firmwares

| | Krähenauge |
|---|---|
| Flash (App) | ~1,24 MB – der Launcher reserviert ~1,25 MB |
| davon eigener Code | ~110 KB, der Rest ist WLAN, TLS, Anzeige- und Hardware-Bibliotheken |
| RAM statisch | ~52 KB |
| RAM zur Laufzeit | Bildpuffer 65 KB, WLAN ~60 KB, je TLS-Verbindung ~45 KB (ntfy immer, System nur solange offen) |

Der ESP32-S3FN8 hat 8 MB Flash und keinen PSRAM. Der M5Launcher (ab 2.8) belegt selbst ~1,4 MB und kann mehrere Firmwares **gleichzeitig** im Flash halten; beim Start wählt man per Taste, welche läuft. Ob Krähenauge neben einer anderen Firmware Platz hat, zeigt der Launcher im **PMan** (Partition Manager; freier Bereich muss ≥ 1,25 MB sein). Große Firmwares mit eigener Daten-Partition können den Rest des Flash belegen – dann wird die jeweils andere beim Wechsel neu von der SD-Karte installiert.

Arbeitsspeicher prüfen: **Einstellungen → System** zeigt freien RAM, den kleinsten Stand seit dem Start und den größten zusammenhängenden Block (eine TLS-Verbindung braucht 16 KB am Stück). Fällt der größte Block unter 20 KB, erscheint eine Warnung. Über USB meldet die Firmware die Werte alle 30 s (`pio device monitor`).

## Bauen

```bash
python3 -m venv ~/.platformio/penv
~/.platformio/penv/bin/pip install platformio
~/.platformio/penv/bin/pio run
```

Ergebnis: `.pio/build/cardputer-adv/firmware.bin`. Serielle Ausgabe über USB: `~/.platformio/penv/bin/pio device monitor`.

## Sicherheit

- **TLS mit Zertifikatsprüfung.** In [`src/certs.h`](src/certs.h) stehen nur ISRG Root X1 und X2 (Let's Encrypt), mit SHA-256-Fingerabdruck. Verbunden wird erst nach erfolgreichem NTP-Abgleich, denn ohne gültige Uhrzeit schlägt die Prüfung fehl.
- **Zugangsdaten nur auf der SD-Karte**, nie in Firmware oder Repo. Wer die Karte hat, hat WLAN-Passwort und Topic.
- **Nur lesend.** Krähenauge ändert nichts auf den Servern. Das Server-Token ist ein Nur-Lese-Token (nur Systemwerte).
- **Token im internen Speicher (NVS), unverschlüsselt.** Der NVS-Bereich wird vom M5Launcher mit allen installierten Firmwares geteilt – jede andere Firmware auf dem Gerät kann ihn auslesen. Deshalb nur ein Nur-Lese-Token. Geht das Gerät verloren: Token auf dem Server widerrufen (Gerät „Krähenauge“). Abmelden in den Einstellungen löscht es auch dort.
- Alle Verbindungen (ntfy, Status, System) laufen über HTTPS mit Prüfung gegen dieselben Stammzertifikate. Server mit Zertifikaten anderer Anbieter als Let's Encrypt werden abgelehnt.

## Aufbau

```
├── platformio.ini
├── .github/workflows/firmware.yml   # Bauen + Release
├── sd-beispiel/raveneye/config.txt  # Vorlage mit Platzhaltern
├── docs/banner/                   # README-Banner + Link-Vorschau (make_banner.py, siehe dortige README)
├── lib/beiwerk/                   # Grove-Zubehör: Erkennung, OLED, Hex-Treiber (Kopie, gleich wie in Takt)
└── src/
    ├── main.cpp          # Start, Hauptschleife, Wechsel zwischen den Funktionen
    ├── boot.cpp/.h       # Startbild
    ├── core.cpp/.h       # Hintergrund: WLAN, ntfy, Meldungen, Einstellungen, Abdunkeln, Speicher
    ├── keys.cpp/.h       # Tastatur mit Wiederholung
    ├── ui.cpp/.h         # Farben, Kopfzeile, Listen, Dialoge, Texteingabe
    ├── text.cpp/.h       # UTF-8, Zeichenvorrat, Zahlen/Zeiten
    ├── net.cpp/.h        # HTTPS + JSON (einmalig oder als offene Verbindung)
    ├── auth.cpp/.h       # Anmeldung mit Nur-Lese-Token
    ├── app_home.cpp      # Startmenü
    ├── app_ntfy.cpp      # Meldungen
    ├── app_status.cpp    # Uptime Kuma
    ├── app_system.cpp    # Systemwerte (Live, Verlauf, Backups)
    ├── app_settings.cpp  # Einstellungen
    ├── zubehoer.cpp/.h   # Anzeige auf Unit OLED / Unit Hex, Hintergrundabfragen dafür
    ├── ntfy.cpp/.h       # ntfy-Stream-Client
    ├── config.cpp/.h     # SD-Konfiguration (Karte wird danach abgemeldet)
    ├── certs.h           # Root-Zertifikate
    └── fonts.c/.h        # Schriften (u8g2-Format, Lizenz im Dateikopf)
```

## Offen

- [ ] Statusanzeige auch im Hintergrund prüfen und bei Ausfall melden
- [ ] System: Warnschwellen (Temperatur, RAM) konfigurierbar machen
- [ ] Falls der RAM knapp wird: Bildpuffer auf 8 Bit Farbtiefe (spart 32 KB)
