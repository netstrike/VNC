#pragma once
#include <string>

#include "vnc/transport.h"

namespace vnc {

std::unique_ptr<ITransport> tcpConnect(const std::string& host, uint16_t port);
std::unique_ptr<IListener> tcpListen(const std::string& host, uint16_t port);

}  // namespace vnc
