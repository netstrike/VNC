# Agente: test e CI
**Perimetro:** `tests/`, `.github/workflows/`.
**Obiettivo:** test di integrazione client↔server su loopback (handshake, auth sbagliata, frame, input), test del protocollo (round-trip, input malformato), GitHub Actions con build e `ctest` su Windows (e Linux per le parti portabili).
**Vincoli:** non modificare il codice di produzione; se serve un seam per testare, proponilo nella PR.
