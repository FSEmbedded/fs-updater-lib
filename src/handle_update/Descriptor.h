#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fs {

enum class MemberType : uint8_t {
    Unknown = 0,
    Firmware,
    Application,
    Manifest,
};

struct Member {
    std::string name;
    MemberType type{MemberType::Unknown};
    uint64_t offset{0};
    uint64_t size{0};
    std::string sha256;
};

/**
 * Parsed v2.0 container descriptor.
 *
 * Mirrors the on-disk JSON shape: a version string, an ordered list of
 * members each with offset/size/sha256, and optional fw/app version
 * strings the CLI surfaces during install.
 */
struct Descriptor {
    std::string version;
    std::vector<Member> members;
    std::string fw_version;
    std::string app_version;
};

/**
 * Parse a v2.0 container descriptor from its on-disk JSON bytes.
 *
 * Throws fs::GenericException with EINVAL on malformed JSON, missing
 * required fields, or an empty members array.
 */
[[nodiscard]] Descriptor parse_descriptor(std::string_view json);

} // namespace fs
