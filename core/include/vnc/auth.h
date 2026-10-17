#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vnc {

// Single fixed-user password authentication. The password itself never goes
// on the wire, even over TLS (defense in depth): the server sends a random
// nonce and the client answers with HMAC-SHA256(derivedKey, nonce), which the
// server can reproduce and compare without exposing the derived key either.

struct Credentials {
  std::vector<uint8_t> salt;        // random, 16 bytes
  uint32_t iterations = 200000;     // PBKDF2 iterations
  std::array<uint8_t, 32> key{};    // PBKDF2-HMAC-SHA256(password, salt, iterations)
};

// Derives `key` from `password` using `salt`/`iterations` already set on `c`.
void deriveKey(Credentials& c, const std::string& password);
// Creates fresh credentials (new random salt) for `password`.
Credentials makeCredentials(const std::string& password);

// Loads/saves credentials to a small text file (salt, iterations, key as hex).
// Throws std::runtime_error if the file is missing or malformed.
Credentials loadCredentials(const std::string& path);
void saveCredentials(const Credentials& c, const std::string& path);

// Default per-platform path for the credentials file / TLS certificate.
std::string defaultConfigDir();

std::array<uint8_t, 16> randomNonce();
std::array<uint8_t, 32> hmacChallenge(const Credentials&, const std::array<uint8_t, 16>& nonce);
// Constant-time compare.
bool secureEqual(const std::array<uint8_t, 32>& a, const std::array<uint8_t, 32>& b);

}  // namespace vnc
