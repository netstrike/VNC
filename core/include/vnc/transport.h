#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace vnc {

// Byte-stream transport. TCP is the first implementation; TLS, QUIC or UDP
// based transports plug in by implementing this interface and registering a
// URI scheme in transport_factory.cpp.
class ITransport {
public:
  virtual ~ITransport() = default;

  // Reads up to `len` bytes. Returns 0 when the peer closed, throws on error.
  virtual size_t read(void* buf, size_t len) = 0;
  // Writes up to `len` bytes, returns the number actually written.
  virtual size_t write(const void* buf, size_t len) = 0;
  virtual void close() = 0;
  virtual std::string peer() const = 0;

  // Helpers: loop until all bytes are transferred. readExact throws on EOF.
  void readExact(void* buf, size_t len);
  void writeAll(const void* buf, size_t len);
};

class IListener {
public:
  virtual ~IListener() = default;
  virtual std::unique_ptr<ITransport> accept() = 0;
  virtual void close() = 0;
  // Actual bound port/endpoint, useful when binding to port 0.
  virtual uint16_t port() const = 0;
};

// URIs: "tcp://host:port". Unknown schemes throw std::runtime_error.
std::unique_ptr<ITransport> connectTo(const std::string& uri);
std::unique_ptr<IListener> listenOn(const std::string& uri);

}  // namespace vnc
