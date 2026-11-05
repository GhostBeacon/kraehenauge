#include "meldestelle.h"

#include <ArduinoJson.h>

#include "auth.h"
#include "certs.h"
#include "net.h"

// Die Meldestelle schickt alle 25 s einen Herzschlag; bleibt es laenger still, ist die Verbindung tot.
static constexpr uint32_t STALL_TIMEOUT_MS = 90000;
static constexpr uint32_t BACKOFF_MAX_MS = 60000;
static constexpr size_t MAX_LINE = 8192;
static constexpr long NACHLADEN = 30;   // so viele aeltere Meldungen beim Start (core haelt max. 30)

static String hinweisFuer(int status) {
    switch (status) {
        case 401: return "Anmeldung abgelaufen – Einstellungen → Server";
        case 403: return "Pi-Status für dieses Konto nicht freigegeben";
        case 503: return "Krähenhort nicht erreichbar";
        default: return "Meldestelle: Fehler " + String(status);
    }
}

void MeldestelleClient::begin(const String& serverUrl, std::function<void(NtfyMessage&&)> onMessage) {
    _url = serverUrl;
    _host = serverUrl.substring(serverUrl.indexOf("://") + 3);
    int slash = _host.indexOf('/');
    if (slash >= 0) _host.remove(slash);
    _onMessage = std::move(onMessage);
    _client.setCACert(ROOT_CAS);
}

// Erster Start: nur die id der neuesten Meldung erfragen, dann ab (id - 30) streamen
bool MeldestelleClient::startpunkt() {
    JsonDocument filter, d;
    filter["letzte_id"] = true;
    net::Result r = net::getJson(_url + "/api/v1/meldungen?seit=2000000000", auth::token(), d, &filter);
    if (!r.ok()) {
        _hinweis = r.status > 0 ? hinweisFuer(r.status) : r.error;
        return false;
    }
    long letzte = d["letzte_id"] | 0L;
    _seit = max(0L, letzte - NACHLADEN);
    return true;
}

bool MeldestelleClient::connect() {
    if (_seit < 0 && !startpunkt()) return false;

    _client.setTimeout(10);  // Sekunden
    if (!_client.connect(_host.c_str(), 443)) {
        _hinweis = "Server nicht erreichbar";
        return false;
    }
    // HTTP/1.0: Caddy schickt den Strom dann ohne Chunked-Kodierung
    _client.printf("GET /api/v1/meldungen/stream?seit=%ld HTTP/1.0\r\n"
                   "Host: %s\r\n"
                   "Authorization: Bearer %s\r\n"
                   "Accept: text/event-stream\r\n"
                   "User-Agent: kraehenauge\r\n"
                   "\r\n",
                   _seit, _host.c_str(), auth::token().c_str());

    String status = _client.readStringUntil('\n');
    int sp = status.indexOf(' ');
    int code = sp > 0 ? status.substring(sp + 1).toInt() : 0;
    if (code != 200) {
        Serial.printf("meldestelle: %s\n", status.c_str());
        _hinweis = code ? hinweisFuer(code) : "Meldestelle antwortet nicht";
        _client.stop();
        return false;
    }
    while (_client.connected()) {   // Kopfzeilen bis zur Leerzeile ueberspringen
        String h = _client.readStringUntil('\n');
        if (h == "\r" || h.isEmpty()) break;
    }
    _buf = _event = _data = "";
    _hinweis = "";
    _lastData = millis();
    return true;
}

void MeldestelleClient::disconnect(bool fehler) {
    _client.stop();
    _state = State::Backoff;
    // Die Meldestelle schliesst jede Verbindung nach einer Stunde – dann gleich wieder verbinden
    _retryAt = millis() + (fehler ? _backoff : 2000);
    if (fehler) _backoff = min(_backoff * 2, BACKOFF_MAX_MS);
}

void MeldestelleClient::poll(bool networkReady) {
    if (!networkReady || !auth::loggedIn()) {
        if (_state == State::Streaming) _client.stop();
        _state = State::Idle;
        _hinweis = networkReady ? "nicht angemeldet – Einstellungen → Server" : "";
        return;
    }

    if (_state == State::Idle || (_state == State::Backoff && (int32_t)(millis() - _retryAt) >= 0)) {
        if (connect()) {
            _state = State::Streaming;
            _backoff = 5000;
        } else {
            disconnect(true);
        }
        return;
    }

    if (_state != State::Streaming) return;

    while (_client.available()) {
        char c = _client.read();
        _lastData = millis();
        if (c == '\n') {
            if (_buf.endsWith("\r")) _buf.remove(_buf.length() - 1);
            zeile(_buf);
            _buf = "";
        } else if (_buf.length() < MAX_LINE) {
            _buf += c;
        }
    }

    if (!_client.connected()) disconnect(false);
    else if (millis() - _lastData > STALL_TIMEOUT_MS) disconnect(true);
}

// Server-Sent Events: "id:", "event:", "data:" sammeln, Leerzeile schliesst das Ereignis ab,
// Zeilen mit ":" sind Kommentare (Herzschlag).
void MeldestelleClient::zeile(const String& z) {
    if (z.isEmpty()) {
        ereignis();
        _event = _data = "";
        return;
    }
    if (z[0] == ':') return;
    int dp = z.indexOf(':');
    String feld = dp < 0 ? z : z.substring(0, dp);
    String wert = dp < 0 ? String() : z.substring(dp + 1);
    if (wert.startsWith(" ")) wert.remove(0, 1);
    if (feld == "event") _event = wert;
    else if (feld == "data") _data += wert;
}

void MeldestelleClient::ereignis() {
    if (_data.isEmpty() || (!_event.isEmpty() && _event != "meldung")) return;
    JsonDocument doc;
    if (deserializeJson(doc, _data)) return;
    long id = doc["id"] | 0L;
    if (id <= 0) return;

    NtfyMessage m;
    m.id = String(id);
    String quelle = doc["quelle"] | "";
    m.topic = String(doc["kanal"] | "") + (quelle.isEmpty() ? "" : " · " + quelle);
    m.title = doc["titel"] | "";
    m.message = doc["text"] | "";
    m.time = doc["zeit"] | 0;
    m.priority = constrain((int)(doc["prio"] | 3), 1, 5);

    if (id > _seit) _seit = id;   // nach Neuverbindung nur Neues holen
    if (_onMessage) _onMessage(std::move(m));
}
