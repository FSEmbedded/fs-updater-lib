#include <gtest/gtest.h>

#include "handle_update/UpdateContainerReader.h"
#include "handle_update/Descriptor.h"
#include "handle_update/fs_exceptions.h"
#include "v2_test_helpers.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sstream>
#include <string>

using fs_test::make_v2_stream;

namespace {

constexpr const char* kValidDescriptor =
    R"json({"version":"2.0","members":[{"name":"x","type":"firmware","offset":200,"size":10,"sha256":"deadbeef"}]})json";

// Payload padding so the declared window [200, 210) lies inside the
// payload region (which starts at 64 + 4 + descriptor length).
std::string payload_for_valid_descriptor()
{
    const std::size_t payload_start = 64 + 4 + std::strlen(kValidDescriptor);
    return std::string(200 - payload_start, '\0') + std::string(10, 'A');
}

} // namespace

TEST(OpenUpdateContainer, ParsesHeaderAndDescriptorFromValidStream)
{
    auto bytes = make_v2_stream(0x20, "FSUPv2", kValidDescriptor,
                                payload_for_valid_descriptor());
    std::istringstream src(bytes, std::ios::binary);

    const auto result = fs::open_update_container(src);

    EXPECT_EQ(result.header.info.version, 0x20);
    EXPECT_EQ(std::memcmp(result.header.info.magic, "FSLX", 4), 0);
    EXPECT_EQ(result.descriptor.version, "2.0");
    ASSERT_EQ(result.descriptor.members.size(), 1u);
    EXPECT_EQ(result.descriptor.members[0].name, "x");
    EXPECT_EQ(result.descriptor.members[0].offset, 200u);
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

TEST(OpenUpdateContainer, RejectsMemberOffsetInsideHeaderOrDescriptor)
{
    // Window [10, 15) starts inside the 64-byte header.
    constexpr const char* desc =
        R"json({"version":"2.0","members":[{"name":"x","type":"firmware","offset":10,"size":5,"sha256":"deadbeef"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, "HELLO");
    std::istringstream src(bytes, std::ios::binary);

    try {
        (void)fs::open_update_container(src);
        FAIL() << "expected GenericException";
    } catch (const fs::GenericException& e) {
        EXPECT_EQ(e.errorno, EINVAL);
    }
}

TEST(OpenUpdateContainer, RejectsMemberWindowPastContainerEnd)
{
    // Offset is inside the payload region but size overruns the
    // container end declared by the header.
    constexpr const char* desc =
        R"json({"version":"2.0","members":[{"name":"x","type":"firmware","offset":400,"size":10000,"sha256":"deadbeef"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, std::string(500, '\0'));
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsMemberOffsetPlusSizeOverflow)
{
    constexpr const char* desc =
        R"json({"version":"2.0","members":[{"name":"x","type":"firmware","offset":18446744073709551611,"size":16,"sha256":"deadbeef"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, std::string(500, '\0'));
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsDuplicateMemberNames)
{
    constexpr const char* desc =
        R"json({"version":"2.0","members":[)json"
        R"json({"name":"x","type":"firmware","offset":0,"size":0,"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},)json"
        R"json({"name":"x","type":"app","offset":0,"size":0,"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, "");
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsTwoMembersOfSameInstallableType)
{
    // Two firmware members would stream to the same sink destination.
    constexpr const char* desc =
        R"json({"version":"2.0","members":[)json"
        R"json({"name":"a","type":"firmware","offset":0,"size":0,"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},)json"
        R"json({"name":"b","type":"firmware","offset":0,"size":0,"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, "");
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsOverlappingMemberWindows)
{
    // [400, 405) and [402, 407) intersect.
    constexpr const char* desc =
        R"json({"version":"2.0","members":[)json"
        R"json({"name":"a","type":"firmware","offset":400,"size":5,"sha256":"deadbeef"},)json"
        R"json({"name":"b","type":"app","offset":402,"size":5,"sha256":"deadbeef"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, std::string(500, '\0'));
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

TEST(OpenUpdateContainer, RejectsMemberNameWithEmbeddedNul)
{
    // jsoncpp expands the u0000 escape into an embedded NUL in the name.
    constexpr const char* desc =
        R"json({"version":"2.0","members":[{"name":"x\u0000y","type":"firmware","offset":200,"size":10,"sha256":"deadbeef"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, std::string(500, '\0'));
    std::istringstream src(bytes, std::ios::binary);

    try {
        (void)fs::open_update_container(src);
        FAIL() << "expected GenericException";
    } catch (const fs::GenericException& e) {
        EXPECT_EQ(e.errorno, EINVAL);
    }
}

TEST(OpenUpdateContainer, RejectsEmptyMemberName)
{
    constexpr const char* desc =
        R"json({"version":"2.0","members":[{"name":"","type":"firmware","offset":200,"size":10,"sha256":"deadbeef"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, std::string(500, '\0'));
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}

TEST(OpenUpdateContainer, RejectsControlCharacterInMemberName)
{
    // A TAB (u0009) is a legal JSON string byte but not a legal name byte.
    constexpr const char* desc =
        R"json({"version":"2.0","members":[{"name":"x\u0009y","type":"firmware","offset":200,"size":10,"sha256":"deadbeef"}]})json";
    auto bytes = make_v2_stream(0x20, "FSUPv2", desc, std::string(500, '\0'));
    std::istringstream src(bytes, std::ios::binary);

    EXPECT_THROW((void)fs::open_update_container(src), fs::GenericException);
}
