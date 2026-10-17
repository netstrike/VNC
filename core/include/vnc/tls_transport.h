#pragma once
#include <memory>
#include <string>

#include "vnc/transport.h"

namespace vnc {

// TLS 1.2+ transport over TCP. The server presents a certificate (generated
// on first run if none exists, see ensureServerCertificate); the client does
// NOT validate it against a CA, since there is none for a self-signed,
// single-machine setup. Instead it uses trust-on-first-use: the certificate's
// SHA-256 fingerprint is pinned to `knownHostsPath` on first connect and
// checked on every later connect, the same model SSH uses for host keys.
// Authentication of the human operator is a separate, explicit step on top
// (see auth.h) — TLS here only gives confidentiality/integrity plus
// protection of that step against a passive eavesdropper.

std::unique_ptr<ITransport> tlsConnect(const std::string& host, uint16_t port,
                                       const std::string& knownHostsPath);
std::unique_ptr<IListener> tlsListen(const std::string& host, uint16_t port,
                                     const std::string& certPath, const std::string& keyPath);

// Generates a self-signed EC (P-256) certificate/key pair at the given paths
// if they do not already exist. No-op otherwise.
void ensureServerCertificate(const std::string& certPath, const std::string& keyPath);

}  // namespace vnc
