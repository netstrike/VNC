#include "vnc/tcp_transport.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalid = INVALID_SOCKET;
static void closeSocket(socket_t s) { ::closesocket(s); }
static void shutdownSocket(socket_t s) { ::shutdown(s, SD_BOTH); }
static std::string lastError() { return "winsock error " + std::to_string(WSAGetLastError()); }
static void initSockets() {
  static bool done = [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); return true; }();
  (void)done;
}
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalid = -1;
static void closeSocket(socket_t s) { ::close(s); }
static void shutdownSocket(socket_t s) { ::shutdown(s, SHUT_RDWR); }
static std::string lastError() { return std::strerror(errno); }
static void initSockets() {}
#endif

namespace vnc {
namespace {

void setNoDelay(socket_t s) {
  int one = 1;
  // Disable Nagle: we favour latency over throughput.
  ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
}

std::string describe(const sockaddr_storage& ss) {
  char host[NI_MAXHOST] = "?", serv[NI_MAXSERV] = "?";
  ::getnameinfo(reinterpret_cast<const sockaddr*>(&ss), sizeof(ss), host, sizeof(host), serv,
                sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
  return std::string(host) + ":" + serv;
}

class TcpTransport : public ITransport {
public:
  TcpTransport(socket_t s, std::string peer) : s_(s), peer_(std::move(peer)) { setNoDelay(s_); }
  ~TcpTransport() override { close(); }

  size_t read(void* buf, size_t len) override {
    for (;;) {
      auto n = ::recv(s_, static_cast<char*>(buf), static_cast<int>(len), 0);
      if (n >= 0) return static_cast<size_t>(n);
#ifndef _WIN32
      if (errno == EINTR) continue;
#endif
      throw std::runtime_error("tcp read: " + lastError());
    }
  }

  size_t write(const void* buf, size_t len) override {
    for (;;) {
#ifdef MSG_NOSIGNAL
      const int flags = MSG_NOSIGNAL;
#else
      const int flags = 0;
#endif
      auto n = ::send(s_, static_cast<const char*>(buf), static_cast<int>(len), flags);
      if (n >= 0) return static_cast<size_t>(n);
#ifndef _WIN32
      if (errno == EINTR) continue;
#endif
      throw std::runtime_error("tcp write: " + lastError());
    }
  }

  void close() override {
    if (s_ != kInvalid) {
      shutdownSocket(s_);
      closeSocket(s_);
      s_ = kInvalid;
    }
  }

  std::string peer() const override { return peer_; }

  socket_t fd() const { return s_; }

private:
  socket_t s_;
  std::string peer_;
};

class TcpListener : public IListener {
public:
  TcpListener(socket_t s, uint16_t port) : s_(s), port_(port) {}
  ~TcpListener() override { close(); }

  std::unique_ptr<ITransport> accept() override {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    socket_t c = ::accept(s_, reinterpret_cast<sockaddr*>(&ss), &len);
    if (c == kInvalid) throw std::runtime_error("tcp accept: " + lastError());
    return std::make_unique<TcpTransport>(c, describe(ss));
  }

  void close() override {
    if (s_ != kInvalid) {
      shutdownSocket(s_);
      closeSocket(s_);
      s_ = kInvalid;
    }
  }

  uint16_t port() const override { return port_; }

private:
  socket_t s_;
  uint16_t port_;
};

struct AddrInfo {
  addrinfo* head = nullptr;
  ~AddrInfo() { if (head) ::freeaddrinfo(head); }
};

AddrInfo resolve(const std::string& host, uint16_t port, bool passive) {
  initSockets();
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (passive) hints.ai_flags = AI_PASSIVE;
  AddrInfo ai;
  const char* h = host.empty() ? nullptr : host.c_str();
  if (::getaddrinfo(h, std::to_string(port).c_str(), &hints, &ai.head) != 0)
    throw std::runtime_error("cannot resolve " + host);
  return ai;
}

}  // namespace

std::unique_ptr<ITransport> tcpConnect(const std::string& host, uint16_t port) {
  AddrInfo ai = resolve(host, port, false);
  for (addrinfo* a = ai.head; a; a = a->ai_next) {
    socket_t s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == kInvalid) continue;
    if (::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0)
      return std::make_unique<TcpTransport>(s, host + ":" + std::to_string(port));
    closeSocket(s);
  }
  throw std::runtime_error("tcp connect to " + host + ":" + std::to_string(port) + " failed");
}

int tcpTransportFd(ITransport& t) {
  auto* tcp = dynamic_cast<TcpTransport*>(&t);
  if (!tcp) throw std::runtime_error("tcpTransportFd: not a TCP transport");
  return static_cast<int>(tcp->fd());
}

std::unique_ptr<IListener> tcpListen(const std::string& host, uint16_t port) {
  AddrInfo ai = resolve(host, port, true);
  for (addrinfo* a = ai.head; a; a = a->ai_next) {
    socket_t s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == kInvalid) continue;
    int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    if (::bind(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) != 0 || ::listen(s, 8) != 0) {
      closeSocket(s);
      continue;
    }
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&ss), &len);
    uint16_t bound = ss.ss_family == AF_INET6
                         ? ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port)
                         : ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
    return std::make_unique<TcpListener>(s, bound);
  }
  throw std::runtime_error("tcp listen on port " + std::to_string(port) + " failed");
}

}  // namespace vnc
