#include <gtest/gtest.h>

#include "handle_update/Descriptor.h"
#include "handle_update/fs_exceptions.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kMinimalV2Json = R"json({
    "version": "2.0",
    "members": [
        {
            "name": "update.fw",
            "type": "firmware",
            "offset": 1024,
            "size": 4096,
            "sha256": "deadbeef"
        }
    ]
})json";

constexpr const char* kCombinedJson = R"json({
    "version": "2.0",
    "fw_version": "1.2.3",
    "app_version": "9.0",
    "members": [
        { "name": "update.fw",  "type": "firmware", "offset": 100, "size": 200, "sha256": "aa" },
        { "name": "update.app", "type": "app",      "offset": 300, "size": 400, "sha256": "bb" }
    ]
})json";

} // namespace

TEST(ParseDescriptor, ParsesMinimalV20Descriptor)
{
    auto d = fs::parse_descriptor(kMinimalV2Json);
    EXPECT_EQ(d.version, "2.0");
    ASSERT_EQ(d.members.size(), 1u);
    EXPECT_EQ(d.members[0].name, "update.fw");
    EXPECT_EQ(d.members[0].type, fs::MemberType::Firmware);
    EXPECT_EQ(d.members[0].offset, 1024u);
    EXPECT_EQ(d.members[0].size, 4096u);
    EXPECT_EQ(d.members[0].sha256, "deadbeef");
}

TEST(ParseDescriptor, ParsesMultipleMembersInDeclaredOrder)
{
    auto d = fs::parse_descriptor(kCombinedJson);
    ASSERT_EQ(d.members.size(), 2u);
    EXPECT_EQ(d.members[0].name, "update.fw");
    EXPECT_EQ(d.members[0].type, fs::MemberType::Firmware);
    EXPECT_EQ(d.members[1].name, "update.app");
    EXPECT_EQ(d.members[1].type, fs::MemberType::Application);
}

TEST(ParseDescriptor, PicksUpOptionalFwAndAppVersion)
{
    auto d = fs::parse_descriptor(kCombinedJson);
    EXPECT_EQ(d.fw_version, "1.2.3");
    EXPECT_EQ(d.app_version, "9.0");
}

TEST(ParseDescriptor, OptionalVersionsDefaultToEmpty)
{
    auto d = fs::parse_descriptor(kMinimalV2Json);
    EXPECT_EQ(d.fw_version, "");
    EXPECT_EQ(d.app_version, "");
}

TEST(ParseDescriptor, MapsKnownMemberTypes)
{
    constexpr const char* json = R"json({
        "version": "2.0",
        "members": [
            { "name": "a", "type": "firmware", "offset": 0, "size": 1, "sha256": "00" },
            { "name": "b", "type": "app",      "offset": 1, "size": 1, "sha256": "00" },
            { "name": "c", "type": "manifest", "offset": 2, "size": 1, "sha256": "00" }
        ]
    })json";
    auto d = fs::parse_descriptor(json);
    ASSERT_EQ(d.members.size(), 3u);
    EXPECT_EQ(d.members[0].type, fs::MemberType::Firmware);
    EXPECT_EQ(d.members[1].type, fs::MemberType::Application);
    EXPECT_EQ(d.members[2].type, fs::MemberType::Manifest);
}

TEST(ParseDescriptor, UnknownMemberTypeBecomesUnknown)
{
    constexpr const char* json = R"json({
        "version": "2.0",
        "members": [
            { "name": "x", "type": "weirdo", "offset": 0, "size": 1, "sha256": "00" }
        ]
    })json";
    auto d = fs::parse_descriptor(json);
    ASSERT_EQ(d.members.size(), 1u);
    EXPECT_EQ(d.members[0].type, fs::MemberType::Unknown);
}

TEST(ParseDescriptor, RejectsMalformedJson)
{
    EXPECT_THROW((void)fs::parse_descriptor("{ this is not json"), fs::GenericException);
}

TEST(ParseDescriptor, RejectsMissingVersionField)
{
    constexpr const char* json = R"json({
        "members": [
            { "name": "x", "type": "firmware", "offset": 0, "size": 1, "sha256": "00" }
        ]
    })json";
    EXPECT_THROW((void)fs::parse_descriptor(json), fs::GenericException);
}

TEST(ParseDescriptor, RejectsMissingMembersArray)
{
    EXPECT_THROW((void)fs::parse_descriptor(R"json({"version": "2.0"})json"),
                 fs::GenericException);
}

TEST(ParseDescriptor, RejectsMemberWithoutRequiredFields)
{
    constexpr const char* json = R"json({
        "version": "2.0",
        "members": [
            { "name": "x", "type": "firmware", "offset": 0 }
        ]
    })json";
    EXPECT_THROW((void)fs::parse_descriptor(json), fs::GenericException);
}

TEST(ParseDescriptor, RejectsEmptyMembersArray)
{
    constexpr const char* json = R"json({
        "version": "2.0",
        "members": []
    })json";
    EXPECT_THROW((void)fs::parse_descriptor(json), fs::GenericException);
}
