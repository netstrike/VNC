#include "vnc/tls_transport.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "vnc/tcp_transport.h"

namespace vnc {
namespace {

void initOpenSsl() {
  static std::once_flag once;
  std::call_once(once, [] {
    SSL_library_init();
    SSL_load_error_strings();
  });
}

[[noreturn]] void throwSslError(const std::string& what) {
  char buf[256];
  unsigned long code = ERR_get_error();
  ERR_error_string_n(code, buf, sizeof(buf));
  throw std::runtime_error("tls: " + what + ": " + buf);
}

struct SslCtxDeleter { void operator()(SSL_CTX* p) const { SSL_CTX_free(p); } };
struct SslDeleter { void operator()(SSL* p) const { SSL_free(p); } };
using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxDeleter>;
using SslPtr = std::unique_ptr<SSL, SslDeleter>;

class TlsTransport : public ITransport {
public:
  TlsTransport(SslCtxPtr ctx, SslPtr ssl, std::unique_ptr<ITransport> tcp)
      : ctx_(std::move(ctx)), ssl_(std::move(ssl)), tcp_(std::move(tcp)) {}

  size_t read(void* buf, size_t len) override {
    int n = SSL_read(ssl_.get(), buf, static_cast<int>(len));
    if (n <= 0) {
      int err = SSL_get_error(ssl_.get(), n);
      if (err == SSL_ERROR_ZERO_RETURN) return 0;
      throwSslError("read");
    }
    return static_cast<size_t>(n);
  }

  size_t write(const void* buf, size_t len) override {
    int n = SSL_write(ssl_.get(), buf, static_cast<int>(len));
    if (n <= 0) throwSslError("write");
    return static_cast<size_t>(n);
  }

  void close() override {
    if (ssl_) SSL_shutdown(ssl_.get());
    if (tcp_) tcp_->close();
  }

  std::string peer() const override { return tcp_->peer(); }

  // SHA-256 fingerprint of the peer certificate, as presented in this
  // session (used for TOFU pinning on the client side).
  std::string peerFingerprint() const {
    X509* cert = SSL_get_peer_certificate(ssl_.get());
    if (!cert) throw std::runtime_error("tls: no peer certificate");
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    X509_digest(cert, EVP_sha256(), digest, &len);
    X509_free(cert);
    static const char* kHex = "0123456789abcdef";
    std::string out(len * 2, '0');
    for (unsigned int i = 0; i < len; ++i) {
      out[2 * i] = kHex[digest[i] >> 4];
      out[2 * i + 1] = kHex[digest[i] & 0xf];
    }
    return out;
  }

private:
  SslCtxPtr ctx_;
  SslPtr ssl_;
  std::unique_ptr<ITransport> tcp_;
};

// --- Known-hosts (TOFU) handling --------------------------------------------

std::string knownHostsKey(const std::string& host, uint16_t port) {
  return host + ":" + std::to_string(port);
}

bool lookupKnownFingerprint(const std::string& path, const std::string& key, std::string& out) {
  std::ifstream f(path);
  std::string k, v;
  while (f >> k >> v) if (k == key) { out = v; return true; }
  return false;
}

void rememberFingerprint(const std::string& path, const std::string& key, const std::string& fp) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream f(path, std::ios::app);
  f << key << " " << fp << "\n";
}

// --- Certificate generation --------------------------------------------------

struct EvpKeyDeleter { void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); } };
struct X509Deleter { void operator()(X509* p) const { X509_free(p); } };

}  // namespace

void ensureServerCertificate(const std::string& certPath, const std::string& keyPath) {
  {
    std::ifstream c(certPath), k(keyPath);
    if (c.good() && k.good()) return;  // already there
  }
  std::filesystem::create_directories(std::filesystem::path(certPath).parent_path());
  initOpenSsl();

  std::unique_ptr<EVP_PKEY, EvpKeyDeleter> pkey(EVP_PKEY_new());
  EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
  if (!pctx || EVP_PKEY_keygen_init(pctx) <= 0 ||
      EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) <= 0) {
    throwSslError("key context init");
  }
  EVP_PKEY* raw = nullptr;
  if (EVP_PKEY_keygen(pctx, &raw) <= 0) throwSslError("EC keygen");
  pkey.reset(raw);
  EVP_PKEY_CTX_free(pctx);

  std::unique_ptr<X509, X509Deleter> x509(X509_new());
  X509_set_version(x509.get(), 2);
  ASN1_INTEGER_set(X509_get_serialNumber(x509.get()), 1);
  X509_gmtime_adj(X509_get_notBefore(x509.get()), 0);
  X509_gmtime_adj(X509_get_notAfter(x509.get()), 10LL * 365 * 24 * 3600);  // 10 years
  X509_set_pubkey(x509.get(), pkey.get());

  X509_NAME* name = X509_get_subject_name(x509.get());
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("vnc-server"), -1, -1, 0);
  X509_set_issuer_name(x509.get(), name);  // self-signed

  if (!X509_sign(x509.get(), pkey.get(), EVP_sha256())) throwSslError("X509_sign");

  FILE* kf = std::fopen(keyPath.c_str(), "wb");
  if (!kf || !PEM_write_PrivateKey(kf, pkey.get(), nullptr, nullptr, 0, nullptr, nullptr)) {
    if (kf) std::fclose(kf);
    throwSslError("writing private key");
  }
  std::fclose(kf);

  FILE* cf = std::fopen(certPath.c_str(), "wb");
  if (!cf || !PEM_write_X509(cf, x509.get())) {
    if (cf) std::fclose(cf);
    throwSslError("writing certificate");
  }
  std::fclose(cf);
}

// --- Listener / connect ------------------------------------------------------

namespace {

class TlsListener : public IListener {
public:
  TlsListener(std::unique_ptr<IListener> tcp, SslCtxPtr ctx)
      : tcp_(std::move(tcp)), ctx_(std::move(ctx)) {}

  std::unique_ptr<ITransport> accept() override {
    auto conn = tcp_->accept();
    int fd = tcpTransportFd(*conn);
    SslPtr ssl(SSL_new(ctx_.get()));
    if (!ssl) throwSslError("SSL_new");
    SSL_set_fd(ssl.get(), fd);
    if (SSL_accept(ssl.get()) != 1) throwSslError("SSL_accept");
    SslCtxPtr noCtx;  // listener keeps the shared ctx alive, not the session
    return std::make_unique<TlsTransport>(std::move(noCtx), std::move(ssl), std::move(conn));
  }

  void close() override { tcp_->close(); }
  uint16_t port() const override { return tcp_->port(); }

private:
  std::unique_ptr<IListener> tcp_;
  SslCtxPtr ctx_;
};

}  // namespace

std::unique_ptr<IListener> tlsListen(const std::string& host, uint16_t port,
                                     const std::string& certPath, const std::string& keyPath) {
  initOpenSsl();
  SslCtxPtr ctx(SSL_CTX_new(TLS_server_method()));
  if (!ctx) throwSslError("SSL_CTX_new");
  SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
  if (SSL_CTX_use_certificate_file(ctx.get(), certPath.c_str(), SSL_FILETYPE_PEM) != 1)
    throwSslError("loading certificate " + certPath);
  if (SSL_CTX_use_PrivateKey_file(ctx.get(), keyPath.c_str(), SSL_FILETYPE_PEM) != 1)
    throwSslError("loading private key " + keyPath);

  auto tcp = tcpListen(host, port);
  return std::make_unique<TlsListener>(std::move(tcp), std::move(ctx));
}

std::unique_ptr<ITransport> tlsConnect(const std::string& host, uint16_t port,
                                       const std::string& knownHostsPath) {
  initOpenSsl();
  SslCtxPtr ctx(SSL_CTX_new(TLS_client_method()));
  if (!ctx) throwSslError("SSL_CTX_new");
  SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
  // No CA validation on purpose (self-signed, single host): see TOFU below.
  SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);

  auto tcp = tcpConnect(host, port);
  int fd = tcpTransportFd(*tcp);
  SslPtr ssl(SSL_new(ctx.get()));
  if (!ssl) throwSslError("SSL_new");
  SSL_set_fd(ssl.get(), fd);
  if (SSL_connect(ssl.get()) != 1) throwSslError("SSL_connect");

  auto t = std::make_unique<TlsTransport>(std::move(ctx), std::move(ssl), std::move(tcp));
  std::string fp = t->peerFingerprint();
  std::string key = knownHostsKey(host, port);
  std::string known;
  if (lookupKnownFingerprint(knownHostsPath, key, known)) {
    if (known != fp) {
      throw std::runtime_error(
          "tls: certificate fingerprint for " + key + " changed (expected " + known + ", got " +
          fp + "). This can mean the server was reinstalled, or someone is intercepting the "
               "connection. Remove the old entry from " + knownHostsPath + " only if you " +
               "recognize the change, then reconnect.");
    }
  } else {
    rememberFingerprint(knownHostsPath, key, fp);
    std::fprintf(stderr, "tls: trusting new certificate for %s (fingerprint %s), saved to %s\n",
                key.c_str(), fp.c_str(), knownHostsPath.c_str());
  }
  return t;
}

}  // namespace vnc
