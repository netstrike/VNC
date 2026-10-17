# Sviluppo in parallelo con più agenti (VSCode + Claude Code)

## Setup
Un worktree git per agente, ognuno aperto in una finestra VSCode separata con la propria sessione Claude Code:

```
git worktree add ../vnc-client       -b feat/client
git worktree add ../vnc-server-win   -b feat/server-windows
git worktree add ../vnc-perf         -b feat/perf
git worktree add ../vnc-tests        -b feat/tests-ci
```

In ogni finestra, primo messaggio a Claude:
> Leggi CLAUDE.md, docs/PROTOCOL.md e docs/agents/<area>.md, poi lavora solo su quel perimetro.

Ogni worktree ha la sua cartella `build/`: non condividerla tra agenti.

## Fasi
1. **Protocollo (agente P, da solo)**: verifica/estende `docs/PROTOCOL.md` e le strutture in `core/include/vnc/protocol.h`. Merge su `main`.
2. **Parallelo**: client, server Windows, prestazioni, test/CI, ciascuno nel proprio perimetro.
3. **Integrazione**: PR piccole su `main`; chi apre la PR fa prima `git fetch && git rebase origin/main`.

## Regole per evitare conflitti
- File condivisi (`core/include/vnc/*.h`, `CMakeLists.txt` radice): modifiche minime e subito in PR dedicata.
- Cambio di protocollo = PR separata di P, che incrementa `kProtocolVersion` e aggiorna la specifica; gli altri agenti poi fanno rebase.
- Se un agente è bloccato da un altro perimetro, lo scrive nella PR/nel messaggio invece di modificare l'altra area.
