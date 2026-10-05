#pragma once
#include <string>

#include "vnc/transport.h"

namespace vnc {

std::unique_ptr<ITransport> tcpConnect(const std::string& host, uint16_t port);
std::unique_ptr<IListener> tcpListen(const std::string& host, uint16_t port);

// Returns the raw socket of a transport created by tcpConnect/tcpListen's
// accept(), so a layer above (TLS) can hand it to its own library. Throws if
// `t` is not actually a TCP transport.
int tcpTransportFd(ITransport& t);

}  // namespace vnc
