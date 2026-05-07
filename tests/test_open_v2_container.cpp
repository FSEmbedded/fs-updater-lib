#include <gtest/gtest.h>

#include "handle_update/UpdateStoreV2.h"
#include "handle_update/Descriptor.h"
#include "handle_update/fs_exceptions.h"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>

namespace {

/// Build a synthetic v2.0 byte stream:
/// [F&S header v1.0 64B] + [u32 LE descriptor length] + [descriptor JSON] + [payload]
std::string make_v2_stream(uint8_t version,
                           const char* type,
                           std::string_view descriptor_json,
                           std::string_view payload_data)
{
    std::string buf(64, '\0');
    std::memcpy(&buf[0], "FSLX", 4);
    // file_size_low/high = bytes after the 64-byte header
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

    // u32 LE descriptor length prefix
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

constexpr const char* kValidDescriptor =
    R"json({"version":"2.0","members":[{"name":"x","type":"firmware","offset":100,"size":10,"sha256":"deadbeef"}]})json";

} // namespace

TEST(OpenV2Container, ParsesHeaderAndDescriptorFromValidStream)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", kValidDescriptor, "");
    std::istringstream src(bytes, std::ios::binary);

    const auto result = fs::open_v2_container(src);

    EXPECT_EQ(result.header.info.version, 0x20);
    EXPECT_EQ(std::memcmp(result.header.info.magic, "FSLX", 4), 0);
    EXPECT_EQ(result.descriptor.version, "2.0");
    ASSERT_EQ(result.descriptor.members.size(), 1u);
    EXPECT_EQ(result.descriptor.members[0].name, "x");
    EXPECT_EQ(result.descriptor.members[0].offset, 100u);
    EXPECT_EQ(result.descriptor.members[0].size, 10u);
}

TEST(OpenV2Container, RejectsLegacyV10Header)
{
    auto bytes = make_v2_stream(0x10, "CERT", "", "");
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_v2_container(src), fs::GenericException);
}

TEST(OpenV2Container, RejectsBadMagic)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", kValidDescriptor, "");
    bytes[0] = 'X';
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_v2_container(src), fs::GenericException);
}

TEST(OpenV2Container, RejectsHeaderShorterThan64Bytes)
{
    std::istringstream src(std::string(20, '\0'), std::ios::binary);

    EXPECT_THROW((void)fs::open_v2_container(src), fs::GenericException);
}

TEST(OpenV2Container, RejectsTruncatedDescriptor)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", kValidDescriptor, "");
    // Lop off the second half of the descriptor bytes
    bytes.resize(64 + 4 + std::strlen(kValidDescriptor) / 2);
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_v2_container(src), fs::GenericException);
}

TEST(OpenV2Container, MalformedDescriptorJsonBubblesUp)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", "{ not json", "");
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_v2_container(src), fs::GenericException);
}

TEST(OpenV2Container, RejectsAbsurdlyLargeDescriptorLengthPrefix)
{
    // Header valid, but length prefix claims 16 MB of descriptor.
    auto bytes = make_v2_stream(0x20, "FSUPv2", "{}", "");
    // Overwrite the u32 LE prefix with a huge value.
    const std::uint32_t huge = 16u * 1024u * 1024u;
    bytes[64] = static_cast<char>(huge & 0xFFu);
    bytes[65] = static_cast<char>((huge >> 8) & 0xFFu);
    bytes[66] = static_cast<char>((huge >> 16) & 0xFFu);
    bytes[67] = static_cast<char>((huge >> 24) & 0xFFu);
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_v2_container(src), fs::GenericException);
}
