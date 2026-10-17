# Protocollo VNC (custom, v1)

Fonte di verità del codice: `core/include/vnc/protocol.h`, `core/src/protocol.cpp`. Questo documento deve restare allineato; ogni cambio passa da una PR dedicata che incrementa `kProtocolVersion`.

## Principi
- Non compatibile con RFB. Lossless: pixel sempre BGRA8, compressi solo con codec lossless.
- Interi little-endian. Messaggio = `u8 type | u32 payload_len | payload`; `payload_len <= kMaxPayload` (256 MiB).
- Trasporto astratto (`ITransport`): il protocollo non dipende da TCP. La sessione è già protetta da TLS (cert pinning) sotto il protocollo.

## Sessione
1. Handshake TLS (fuori protocollo).
2. `Hello` in entrambe le direzioni (versione; il server indica le dimensioni del desktop).
3. `AuthChallenge` (server→client): salt PBKDF2, iterazioni, nonce da 16 byte.
4. `AuthResponse` (client→server): HMAC-SHA256(chiave derivata, nonce). La password non viaggia mai.
5. `AuthResult` (server→client): 1 byte, 1 = ok.
6. Flusso: `FrameUpdate` (server→client); `KeyEvent`, `PointerEvent` (client→server); `Clipboard` (bidirezionale); `Bye`.

## Messaggi
| Tipo | Valore | Direzione |
|---|---|---|
| Hello | 1 | entrambe |
| AuthChallenge | 2 | S→C |
| AuthResponse | 3 | C→S |
| AuthResult | 4 | S→C |
| FrameUpdate | 5 | S→C |
| KeyEvent | 6 | C→S |
| PointerEvent | 7 | C→S |
| Clipboard | 8 | entrambe |
| Bye | 9 | entrambe |

`FrameUpdate`: dimensione completa del desktop + lista di `Rect` (x, y, w, h, encoding, dati). Encoding: `Raw` (0, w·h·4 byte) o `Zstd` (1, lossless, vedi `vnc/compress.h`).
`KeyEvent`: scancode indipendente dalla piattaforma + down/up. `PointerEvent`: x, y, bottoni (bit0 sx, bit1 centro, bit2 dx), rotella.

## Estensioni previste (da decidere in PR dedicate)
- Selezione/elenco monitor lato client (oggi lato server: `ICaptureSource::selectMonitors`).
- Negoziazione di encoding/livello di compressione, ack di frame per controllo del flusso.
- Heartbeat/misura RTT.
