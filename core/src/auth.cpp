#include "vnc/auth.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace vnc {
namespace {

std::string toHex(const uint8_t* data, size_t len) {
  static const char* kHex = "0123456789abcdef";
  std::string out(len * 2, '0');
  for (size_t i = 0; i < len; ++i) {
    out[2 * i] = kHex[data[i] >> 4];
    out[2 * i + 1] = kHex[data[i] & 0xf];
  }
  return out;
}

std::vector<uint8_t> fromHex(const std::string& s) {
  if (s.size() % 2) throw std::runtime_error("auth: malformed hex");
  std::vector<uint8_t> out(s.size() / 2);
  for (size_t i = 0; i < out.size(); ++i)
    out[i] = static_cast<uint8_t>(std::stoul(s.substr(2 * i, 2), nullptr, 16));
  return out;
}

}  // namespace

void deriveKey(Credentials& c, const std::string& password) {
  if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), c.salt.data(),
                        static_cast<int>(c.salt.size()), static_cast<int>(c.iterations),
                        EVP_sha256(), static_cast<int>(c.key.size()), c.key.data()) != 1)
    throw std::runtime_error("auth: PBKDF2 failed");
}

Credentials makeCredentials(const std::string& password) {
  Credentials c;
  c.salt.resize(16);
  if (RAND_bytes(c.salt.data(), static_cast<int>(c.salt.size())) != 1)
    throw std::runtime_error("auth: RAND_bytes failed");
  deriveKey(c, password);
  return c;
}

void saveCredentials(const Credentials& c, const std::string& path) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream f(path, std::ios::trunc);
  if (!f) throw std::runtime_error("auth: cannot write " + path);
  f << "iterations " << c.iterations << "\n";
  f << "salt " << toHex(c.salt.data(), c.salt.size()) << "\n";
  f << "key " << toHex(c.key.data(), c.key.size()) << "\n";
}

Credentials loadCredentials(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("auth: cannot read " + path);
  Credentials c;
  std::string tag, value;
  bool haveSalt = false, haveKey = false;
  while (f >> tag >> value) {
    if (tag == "iterations") c.iterations = static_cast<uint32_t>(std::stoul(value));
    else if (tag == "salt") { c.salt = fromHex(value); haveSalt = true; }
    else if (tag == "key") {
      auto bytes = fromHex(value);
      if (bytes.size() != c.key.size()) throw std::runtime_error("auth: bad key length");
      std::copy(bytes.begin(), bytes.end(), c.key.begin());
      haveKey = true;
    }
  }
  if (!haveSalt || !haveKey) throw std::runtime_error("auth: incomplete credentials file");
  return c;
}

std::string defaultConfigDir() {
#ifdef _WIN32
  const char* base = std::getenv("ProgramData");
  std::string dir = (base ? std::string(base) : "C:\\ProgramData") + "\\vnc";
#else
  const char* home = std::getenv("HOME");
  std::string dir = (home ? std::string(home) : "/root") + "/.config/vnc";
#endif
  return dir;
}

std::array<uint8_t, 16> randomNonce() {
  std::array<uint8_t, 16> n{};
  if (RAND_bytes(n.data(), static_cast<int>(n.size())) != 1)
    throw std::runtime_error("auth: RAND_bytes failed");
  return n;
}

std::array<uint8_t, 32> hmacChallenge(const Credentials& c, const std::array<uint8_t, 16>& nonce) {
  std::array<uint8_t, 32> out{};
  unsigned int len = 0;
  if (!HMAC(EVP_sha256(), c.key.data(), static_cast<int>(c.key.size()), nonce.data(), nonce.size(),
           out.data(), &len) || len != out.size())
    throw std::runtime_error("auth: HMAC failed");
  return out;
}

bool secureEqual(const std::array<uint8_t, 32>& a, const std::array<uint8_t, 32>& b) {
  uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

}  // namespace vnc
