#include <gtest/gtest.h>

#include "handle_update/UpdateContainerReader.h"
#include "handle_update/Descriptor.h"
#include "handle_update/fs_exceptions.h"
#include "v2_test_helpers.h"

#include <cstring>
#include <sstream>
#include <string>

using fs_test::make_v2_stream;

namespace {

constexpr const char* kValidDescriptor =
    R"json({"version":"2.0","members":[{"name":"x","type":"firmware","offset":100,"size":10,"sha256":"deadbeef"}]})json";

} // namespace

TEST(OpenUpdateContainer, ParsesHeaderAndDescriptorFromValidStream)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", kValidDescriptor, "");
    std::istringstream src(bytes, std::ios::binary);

    const auto result = fs::open_update_container(src);

    EXPECT_EQ(result.header.info.version, 0x20);
    EXPECT_EQ(std::memcmp(result.header.info.magic, "FSLX", 4), 0);
    EXPECT_EQ(result.descriptor.version, "2.0");
    ASSERT_EQ(result.descriptor.members.size(), 1u);
    EXPECT_EQ(result.descriptor.members[0].name, "x");
    EXPECT_EQ(result.descriptor.members[0].offset, 100u);
    EXPECT_EQ(result.descriptor.members[0].size, 10u);
}

TEST(OpenUpdateContainer, RejectsLegacyV10Header)
{
    auto bytes = make_v2_stream(0x10, "CERT", "", "");
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsBadMagic)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", kValidDescriptor, "");
    bytes[0] = 'X';
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsHeaderShorterThan64Bytes)
{
    std::istringstream src(std::string(20, '\0'), std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsTruncatedDescriptor)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", kValidDescriptor, "");
    // Lop off the second half of the descriptor bytes
    bytes.resize(64 + 4 + std::strlen(kValidDescriptor) / 2);
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, MalformedDescriptorJsonBubblesUp)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", "{ not json", "");
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsAbsurdlyLargeDescriptorLengthPrefix)
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

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}
