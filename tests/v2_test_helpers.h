#pragma once

#include "handle_update/Sha256Hasher.h"
#include "handle_update/UpdateStreamSink.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <unistd.h>

namespace fs_test {

/// Build a synthetic v2.0 byte stream:
/// `[F&S header v1.0 64B] + [u32 LE descriptor length] + [descriptor JSON] + [payload]`
inline std::string make_v2_stream(uint8_t version,
                                  const char* type,
                                  std::string_view descriptor_json,
                                  std::string_view payload_data)
{
    std::string buf(64, '\0');
    std::memcpy(&buf[0], "FSLX", 4);
    const std::uint64_t after_header =
        static_cast<std::uint64_t>(4 + descriptor_json.size() + payload_data.size());
    const auto lo = static_cast<std::uint32_t>(after_header & 0xFFFFFFFFu);
    const auto hi = static_cast<std::uint32_t>(after_header >> 32);
    std::memcpy(&buf[4], &lo, 4);
    std::memcpy(&buf[8], &hi, 4);
    buf[15] = static_cast<char>(version);
    if (type != nullptr) {
        std::strncpy(&buf[16], type, 16);
    }

    const auto desc_len = static_cast<std::uint32_t>(descriptor_json.size());
    char lenbuf[4];
    lenbuf[0] = static_cast<char>(desc_len & 0xFFu);
    lenbuf[1] = static_cast<char>((desc_len >> 8) & 0xFFu);
    lenbuf[2] = static_cast<char>((desc_len >> 16) & 0xFFu);
    lenbuf[3] = static_cast<char>((desc_len >> 24) & 0xFFu);
    buf.append(lenbuf, 4);
    buf.append(descriptor_json);
    buf.append(payload_data);
    return buf;
}

/// SHA-256 of `s`, lowercase hex (uses Sha256Hasher).
inline std::string sha256_of(std::string_view s)
{
    fs::Sha256Hasher h;
    h.update(s.data(), s.size());
    return h.hex_digest();
}

/// Write `content` to a unique temp path under /tmp; returns the path.
/// Caller is responsible for `std::remove(path.c_str())` after use.
inline std::string write_temp_file(const std::string& content, const char* tag)
{
    std::string path = "/tmp/fs-updater-lib-test-";
    path += tag;
    path += "-";
    path += std::to_string(::getpid());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

/// Sink that captures every write + counts commit/abort calls.
class RecordingSink : public fs::UpdateStreamSink
{
public:
    std::string buffer;
    int commit_calls = 0;
    int abort_calls = 0;

    void write(const char* d, std::size_t n) override { buffer.append(d, n); }
    void commit() override { ++commit_calls; }
    void abort() override { ++abort_calls; }
};

} // namespace fs_test
