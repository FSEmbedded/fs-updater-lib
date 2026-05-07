#include <gtest/gtest.h>

#include "handle_update/UpdateStoreV2.h"

#include <cstring>

namespace {

fs::fs_header_v1_0 make_header(uint8_t version, const char* type)
{
    fs::fs_header_v1_0 h{};
    std::memcpy(h.info.magic, "FSLX", 4);
    h.info.version = version;
    if (type != nullptr) {
        std::strncpy(h.type, type, sizeof(h.type));
    }
    return h;
}

} // namespace

TEST(UpdateStoreV2_DetectFormatVersion, V20WithFSUPv2Type)
{
    auto h = make_header(0x20, "FSUPv2");
    EXPECT_EQ(fs::detect_format_version(h), fs::FormatVersion::V2_0);
}

TEST(UpdateStoreV2_DetectFormatVersion, V10IsLegacy)
{
    auto h = make_header(0x10, "CERT");
    EXPECT_EQ(fs::detect_format_version(h), fs::FormatVersion::V1_0);
}

TEST(UpdateStoreV2_DetectFormatVersion, BadMagicIsInvalid)
{
    auto h = make_header(0x20, "FSUPv2");
    std::memcpy(h.info.magic, "XXXX", 4);
    EXPECT_EQ(fs::detect_format_version(h), fs::FormatVersion::Invalid);
}

TEST(UpdateStoreV2_DetectFormatVersion, V20WithoutFSUPv2TypeIsInvalid)
{
    auto h = make_header(0x20, "OTHER");
    EXPECT_EQ(fs::detect_format_version(h), fs::FormatVersion::Invalid);
}

TEST(UpdateStoreV2_DetectFormatVersion, UnknownVersionIsInvalid)
{
    auto h = make_header(0x30, "FSUPv2");
    EXPECT_EQ(fs::detect_format_version(h), fs::FormatVersion::Invalid);
}
