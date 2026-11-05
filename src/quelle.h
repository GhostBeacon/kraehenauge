// Gemeinsame Schnittstelle der Meldungsquellen: Meldestelle (eigener Server) oder ntfy.
// Beide liefern NtfyMessage-Objekte an core (Name aus der ntfy-Zeit, Inhalt allgemein).
#pragma once

#include <Arduino.h>

class Quelle {
public:
    enum class State { Idle, Streaming, Backoff };

    virtual ~Quelle() = default;
    virtual void poll(bool networkReady) = 0;   // in jedem loop(); nur der Verbindungsaufbau blockiert kurz
    virtual State state() const = 0;
    virtual const char* name() const = 0;       // fuer Anzeigen: "Meldestelle", "ntfy"
    virtual String hinweis() const { return String(); }   // warum gerade nichts ankommt (leer = alles gut)
};
