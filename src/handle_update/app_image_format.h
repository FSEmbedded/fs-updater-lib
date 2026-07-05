// Application-image on-disk format: header constants + big-endian parser.
// Dependency-free so the verification TU (cert_image_verifier.cpp) and the
// native tests can include it without dragging the engine headers (UBoot).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace updater::config {
    constexpr char APP_UPDATE[] = "application update";

    // Image format constants
    constexpr std::size_t HEADER_SIZE = 16;
    constexpr std::size_t CHUNK_SIZE = 4096;
    constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;
    constexpr uint32_t CRC32_INITIAL = 0xFFFFFFFF;
}

namespace updater {

    // Separate header parsing class
    class HeaderParser {
    public:
        struct ImageHeader {
            uint64_t squashfs_size;
            uint32_t version;
            uint32_t crc;

            [[nodiscard]] bool is_valid() const {
                return squashfs_size > 0 && version > 0;
            }
        };

        static ImageHeader parse(const std::vector<uint8_t>& header_data);
        static bool validate_crc(const ImageHeader& header,
                               const std::vector<uint8_t>& header_data);
    };

} // namespace updater
