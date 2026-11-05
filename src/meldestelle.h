// Meldungen von der Meldestelle des eigenen Servers – derselbe Weg wie die Krähenhort-App:
//   GET <server_url>/api/v1/meldungen?seit=<sehr gross>   -> {"letzte_id": n}  (Startpunkt)
//   GET <server_url>/api/v1/meldungen/stream?seit=<id>    -> Server-Sent Events, live
// Anmeldung mit dem Nur-Lese-Token aus auth (die Meldestelle prueft es beim Kraehenhort, erlaubt
// sind Konten mit freigegebenem Pi-Status). Beim Start kommen die letzten 30 Meldungen mit.
// Nicht blockierend: poll() in jedem loop(). Nur der Verbindungsaufbau blockiert kurz.
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <functional>

#include "ntfy.h"     // NtfyMessage
#include "quelle.h"

class MeldestelleClient : public Quelle {
public:
    void begin(const String& serverUrl, std::function<void(NtfyMessage&&)> onMessage);
    void poll(bool networkReady) override;
    State state() const override { return _state; }
    const char* name() const override { return "Meldestelle"; }
    String hinweis() const override { return _hinweis; }

private:
    bool startpunkt();
    bool connect();
    void disconnect(bool fehler);
    void zeile(const String& z);
    void ereignis();

    WiFiClientSecure _client;
    String _url;               // https://host (ohne Pfad)
    String _host;
    long _seit = -1;           // letzte bekannte id, -1 = Startpunkt noch holen
    String _buf, _event, _data;
    String _hinweis;
    State _state = State::Idle;
    uint32_t _lastData = 0;
    uint32_t _retryAt = 0;
    uint32_t _backoff = 5000;
    std::function<void(NtfyMessage&&)> _onMessage;
};
