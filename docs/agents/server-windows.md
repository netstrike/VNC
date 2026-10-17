# Agente: server Windows
**Perimetro:** `server/`, `platform/windows/`.
**Obiettivo:** il server deve mostrare esattamente il monitor fisico, con o senza utente loggato (login/lock screen) e con sessione RDP attiva; funzionare come servizio; multi-monitor; iniezione di input anche su desktop sicuro (UAC/Winlogon).
**Vincoli:** mantieni `ICaptureSource`/`IInputSink` come unica superficie verso `core`; nessun codice Win32 fuori da `platform/windows`.
**Fatto quando:** casi login screen, lock screen, RDP, cambio risoluzione e hot-plug monitor documentati e verificati (con note su cosa è testabile solo a mano).
