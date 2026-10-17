# VNC — istruzioni per Claude

Server e client di desktop remoto simili a RealVNC, in C++20 con CMake.
Rispondi e documenta in **italiano** (codice, commit e titoli delle PR seguono la convenzione del repo).

## Requisiti non negoziabili
- Protocollo **custom** (non RFB): niente compatibilità con client/server VNC esistenti.
- **Bassa latenza** e compressione **lossless** (Zstd). Vietati H.264/VP9 e ogni codec lossy.
- Trasporto **pluggable** dietro `ITransport` (oggi TCP + TLS con cert pinning); autenticazione a password fissa, utente singolo.
- Il server Windows mostra sempre il monitor fisico, anche con login/lock screen o sessione RDP attiva.
- **Per ora solo Windows**, ma il codice specifico di piattaforma resta in `platform/<os>/` dietro le interfacce di `core/` (`ICaptureSource`, `IInputSink`, `ITransport`).

## Struttura
- `core/` — protocollo, trasporti, auth, compressione, diff dei tile (indipendente dalla piattaforma)
- `platform/windows`, `platform/linux` — cattura schermo, input, servizio
- `server/`, `client/` — eseguibili
- `tests/` — test (`ctest`)
- `docs/PROTOCOL.md` — specifica del protocollo; `docs/PARALLEL_WORKFLOW.md` — come lavorano gli agenti in parallelo; `docs/agents/*.md` — perimetro di ogni agente

## Build e test
```
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```
Esegui build e test prima di ogni commit. Non lasciare i test rossi.

## Regole di lavoro
- Lavora solo nel perimetro assegnato (vedi `docs/agents/`). Se serve toccare un'altra area o cambiare il protocollo, fermati e segnalalo nella PR: il protocollo cambia solo con una PR dedicata che aggiorna anche `docs/PROTOCOL.md`.
- Una branch per agente (`feat/<area>`), PR piccole e frequenti verso `main`, rebase su `main` prima di aprirle.
- Nessuna dipendenza nuova senza motivarla nella PR.
- Mantieni lo stile del codice circostante; commenti solo dove il perché non è ovvio.
