#include "Sha256Hasher.h"

#include <botan/hash.h>
#include <botan/hex.h>

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace fs {

Sha256Hasher::Sha256Hasher()
    : hash_(Botan::HashFunction::create("SHA-256"))
{
}

Sha256Hasher::~Sha256Hasher() = default;

void Sha256Hasher::update(const char* data, std::size_t n)
{
    hash_->update(reinterpret_cast<const uint8_t*>(data), n);
}

std::string Sha256Hasher::hex_digest()
{
    const auto digest = hash_->final();
    std::string hex = Botan::hex_encode(digest);
    std::transform(hex.begin(), hex.end(), hex.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return hex;
}

} // namespace fs
