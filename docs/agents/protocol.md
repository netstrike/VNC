# Agente P: protocollo
**Perimetro:** `core/include/vnc/protocol.h`, `core/src/protocol.cpp`, `docs/PROTOCOL.md`.
**Obiettivo:** tenere specifica e codice allineati, definire le estensioni in `docs/PROTOCOL.md` ("Estensioni previste"), incrementare `kProtocolVersion` a ogni cambio incompatibile.
**Regole:** parte da solo prima degli altri; dopo, interviene solo con PR dedicate. Ogni cambio ha test di round-trip e input malformato (lunghezze oltre `kMaxPayload`, payload troncati).
