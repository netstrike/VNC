# Agente: client
**Perimetro:** `client/`, parti client-only di `platform/windows`. Non modificare `core/` salvo bug evidenti (segnalali).
**Obiettivo:** client Windows utilizzabile: finestra che mostra i `FrameUpdate` (decompressi con `decompressRect`), invio di mouse e tastiera (`KeyEvent`/`PointerEvent`), clipboard, riconnessione automatica, pinning del certificato.
**Vincoli:** bassa latenza (decodifica fuori dal thread UI, niente copie inutili); codice Win32 isolato in `platform/windows` dietro un'interfaccia (es. `IDisplayWindow`) così il porting resta facile.
**Fatto quando:** il client si connette al server, mostra lo schermo, input funzionante, test dove possibile senza GUI.
