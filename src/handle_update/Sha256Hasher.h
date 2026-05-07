#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace Botan { class HashFunction; }

namespace fs {

/**
 * Streaming SHA-256 hasher used by the v2.0 reader to verify each
 * member's digest inline while bytes flow to the destination sink.
 *
 * Hides Botan behind a forward declaration so consumers don't drag
 * `botan/hash.h` transitively. Single-shot: after `hex_digest()` the
 * hasher is consumed and must be reconstructed for further use.
 */
class Sha256Hasher
{
public:
    Sha256Hasher();
    ~Sha256Hasher();

    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;
    Sha256Hasher(Sha256Hasher&&) = delete;
    Sha256Hasher& operator=(Sha256Hasher&&) = delete;

    void update(const char* data, std::size_t n);

    /** Finalize and return the digest as 64 lowercase hex chars. */
    [[nodiscard]] std::string hex_digest();

private:
    std::unique_ptr<Botan::HashFunction> hash_;
};

} // namespace fs
