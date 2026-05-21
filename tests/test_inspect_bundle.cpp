#include <gtest/gtest.h>

#include "handle_update/inspect_bundle.h"
#include "v2_test_helpers.h"

#include <cstdio>
#include <fstream>
#include <string>

using fs_test::make_v2_stream;
using fs_test::write_temp_file;

namespace {

// Build a one-member descriptor with explicit member type + size + offset.
// Offset/size don't have to be valid for inspect_bundle — it only reads
// the header + descriptor, never seeks to members.
std::string one_member_descriptor(const char* member_type, const char* version)
{
    std::string out = R"json({"version":")json";
    out += version;
    out += R"json(","members":[{"name":"x","type":")json";
    out += member_type;
    out += R"json(","offset":0,"size":0,"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}]})json";
    return out;
}

} // namespace

TEST(InspectBundle, NonExistentPathReportsInvalid)
{
    const auto info = fs::inspect_bundle("/tmp/fs-updater-lib-no-such-file-XYZ-12345.fs");
    EXPECT_FALSE(info.valid);
    EXPECT_EQ(info.size, 0u);
    EXPECT_TRUE(info.update_type.empty());
    EXPECT_TRUE(info.version.empty());
}

TEST(InspectBundle, NonV2FileReportsValidButEmpty)
{
    // Arbitrary content — not an FSLX header.
    const std::string content = "this is definitely not a v2.0 bundle";
    const auto path = write_temp_file(content, "inspect-nonv2");

    const auto info = fs::inspect_bundle(path);
    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.size, content.size());
    EXPECT_TRUE(info.update_type.empty());
    EXPECT_TRUE(info.version.empty());

    std::remove(path.c_str());
}

TEST(InspectBundle, FwBundleReportsFwAndVersion)
{
    const auto desc = one_member_descriptor("firmware", "1.2.3");
    const auto bytes = make_v2_stream(0x20, "FSUPv2", desc, "");
    const auto path = write_temp_file(bytes, "inspect-fw");

    const auto info = fs::inspect_bundle(path);
    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.update_type, "fw");
    EXPECT_EQ(info.version, "1.2.3");
    EXPECT_EQ(info.size, bytes.size());

    std::remove(path.c_str());
}

TEST(InspectBundle, AppBundleReportsApp)
{
    const auto desc = one_member_descriptor("app", "9.9");
    const auto bytes = make_v2_stream(0x20, "FSUPv2", desc, "");
    const auto path = write_temp_file(bytes, "inspect-app");

    const auto info = fs::inspect_bundle(path);
    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.update_type, "app");
    EXPECT_EQ(info.version, "9.9");

    std::remove(path.c_str());
}

TEST(InspectBundle, FwPlusAppBundleReportsCombinedType)
{
    // Two members: one firmware, one application.
    constexpr const char* desc = R"json({
        "version":"2.0",
        "members":[
            {"name":"f","type":"firmware","offset":0,"size":0,
             "sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
            {"name":"a","type":"app","offset":0,"size":0,
             "sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}
        ]})json";
    const auto bytes = make_v2_stream(0x20, "FSUPv2", desc, "");
    const auto path = write_temp_file(bytes, "inspect-fwapp");

    const auto info = fs::inspect_bundle(path);
    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.update_type, "fw+app");
    EXPECT_EQ(info.version, "2.0");

    std::remove(path.c_str());
}

TEST(InspectBundle, ManifestOnlyBundleHasEmptyType)
{
    // Only a Manifest member — neither fw nor app. Inspect should still
    // succeed (valid=true) but leave update_type empty so the install
    // worker can reject it.
    const auto desc = one_member_descriptor("manifest", "0.1");
    const auto bytes = make_v2_stream(0x20, "FSUPv2", desc, "");
    const auto path = write_temp_file(bytes, "inspect-manifest");

    const auto info = fs::inspect_bundle(path);
    EXPECT_TRUE(info.valid);
    EXPECT_TRUE(info.update_type.empty());
    EXPECT_EQ(info.version, "0.1");

    std::remove(path.c_str());
}

TEST(InspectBundle, V10BundleReturnsValidButNoMetadata)
{
    // Legacy v1.0 file — FSLX header at version byte 0x10. open() throws.
    // inspect_bundle catches and reports valid=true with empty fields.
    const auto bytes = make_v2_stream(0x10, "CERT", "", "");
    const auto path = write_temp_file(bytes, "inspect-v1");

    const auto info = fs::inspect_bundle(path);
    EXPECT_TRUE(info.valid);
    EXPECT_TRUE(info.update_type.empty());
    EXPECT_TRUE(info.version.empty());
    EXPECT_EQ(info.size, bytes.size());

    std::remove(path.c_str());
}
