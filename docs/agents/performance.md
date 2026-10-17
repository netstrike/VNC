# Agente: prestazioni
**Perimetro:** `core/src/tile_diff.cpp`, `core/src/compress.cpp`, pipeline di invio in `server/`.
**Obiettivo:** ridurre la latenza end-to-end restando lossless: rilevamento delle sole regioni cambiate, merge dei rettangoli, livello Zstd adattivo, pipeline asincrona cattura→diff→compressione→invio, controllo del flusso.
**Metodo:** prima un benchmark riproducibile (latenza per frame, banda, CPU), poi le ottimizzazioni, ognuna con numeri prima/dopo nella PR.
**Vincoli:** niente codec lossy; cambi al formato su rete = richiesta all'agente protocollo.
