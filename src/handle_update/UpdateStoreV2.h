#pragma once

#include "fs_header_types.h"

#include <cstdint>
#include <cstring>

namespace fs {

/**
 * On-disk format of an `.fs` update container, as discriminated by the
 * F&S header byte at offset 15 (`info.version`) and the `type[16]` field.
 *
 * - V1_0: legacy `[fs_header v1.0] + tar.bz2` payload. Read by the
 *   existing UpdateStore::ExtractUpdateStore code path.
 * - V2_0: streaming `[fs_header v2.0] + length-prefixed JSON descriptor +
 *   raw concatenated members`. Read by UpdateStoreV2.
 * - Invalid: anything else (bad magic, unknown version byte, v2.0 header
 *   with the wrong type tag).
 */
enum class FormatVersion : uint8_t {
    Invalid = 0,
    V1_0,
    V2_0,
};

inline constexpr uint8_t FS_HEADER_VERSION_V1_0 = 0x10;
inline constexpr uint8_t FS_HEADER_VERSION_V2_0 = 0x20;

/**
 * Identify the update-container format from a parsed F&S header.
 *
 * Pure function: no I/O, no allocation. Caller is responsible for having
 * read the 64-byte header from the source stream into `header`.
 */
[[nodiscard]] inline FormatVersion detect_format_version(const fs_header_v1_0& header) noexcept
{
    if (std::memcmp(header.info.magic, "FSLX", 4) != 0) {
        return FormatVersion::Invalid;
    }
    switch (header.info.version) {
        case FS_HEADER_VERSION_V1_0:
            return FormatVersion::V1_0;
        case FS_HEADER_VERSION_V2_0:
            // v2.0 is only valid when the type tag matches; any other
            // type at version 0x20 is reserved for future variants.
            if (std::strncmp(header.type, "FSUPv2", 6) == 0) {
                return FormatVersion::V2_0;
            }
            return FormatVersion::Invalid;
        default:
            return FormatVersion::Invalid;
    }
}

} // namespace fs
