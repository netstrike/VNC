#include <stdexcept>
#include <string>

#include "vnc/tcp_transport.h"
#include "vnc/transport.h"

namespace vnc {

void ITransport::readExact(void* buf, size_t len) {
  auto* p = static_cast<uint8_t*>(buf);
  while (len) {
    size_t n = read(p, len);
    if (n == 0) throw std::runtime_error("transport: connection closed");
    p += n;
    len -= n;
  }
}

void ITransport::writeAll(const void* buf, size_t len) {
  auto* p = static_cast<const uint8_t*>(buf);
  while (len) {
    size_t n = write(p, len);
    p += n;
    len -= n;
  }
}

namespace {

struct Uri {
  std::string scheme, host;
  uint16_t port = 0;
};

Uri parse(const std::string& uri) {
  auto sep = uri.find("://");
  if (sep == std::string::npos) throw std::runtime_error("bad uri: " + uri);
  Uri u;
  u.scheme = uri.substr(0, sep);
  std::string rest = uri.substr(sep + 3);
  auto colon = rest.rfind(':');
  if (colon == std::string::npos) throw std::runtime_error("uri needs a port: " + uri);
  u.host = rest.substr(0, colon);
  u.port = static_cast<uint16_t>(std::stoi(rest.substr(colon + 1)));
  return u;
}

}  // namespace

std::unique_ptr<ITransport> connectTo(const std::string& uri) {
  Uri u = parse(uri);
  if (u.scheme == "tcp") return tcpConnect(u.host, u.port);
  throw std::runtime_error("unsupported transport scheme: " + u.scheme);
}

std::unique_ptr<IListener> listenOn(const std::string& uri) {
  Uri u = parse(uri);
  if (u.scheme == "tcp") return tcpListen(u.host, u.port);
  throw std::runtime_error("unsupported transport scheme: " + u.scheme);
}

}  // namespace vnc
