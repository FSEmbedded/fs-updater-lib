#include <gtest/gtest.h>

#include "handle_update/sources/UpdateSource.h"
#include "v2_test_helpers.h"

#include <cstring>
#include <string>

namespace {

fs::FormatProbe probe_of(const std::string& s)
{
    return fs::FormatProbe{reinterpret_cast<const unsigned char*>(s.data()), s.size()};
}

// --- the magic-at-offset-0 formats -----------------------------------------

TEST(DetectUpdateFormat, V2ContainerIsContainer)
{
    // FSLX magic + version byte 0x20 + type "FSUPv2"
    const std::string s = fs_test::make_v2_stream(0x20, "FSUPv2", "{}", "");
    EXPECT_EQ(fs::detect_update_format(probe_of(s)), fs::UpdateFormat::Container);
}

TEST(DetectUpdateFormat, V10HeaderIsLegacyTarball)
{
    // FSLX magic + version byte 0x10 (legacy v1.0 container)
    const std::string s = fs_test::make_v2_stream(0x10, nullptr, "", "");
    EXPECT_EQ(fs::detect_update_format(probe_of(s)), fs::UpdateFormat::LegacyTarball);
}

TEST(DetectUpdateFormat, V20WithWrongTypeIsUnknown)
{
    // version 0x20 but type tag is not "FSUPv2" -> reserved/unknown
    const std::string s = fs_test::make_v2_stream(0x20, "NOTFSUP", "{}", "");
    EXPECT_EQ(fs::detect_update_format(probe_of(s)), fs::UpdateFormat::Unknown);
}

TEST(DetectUpdateFormat, SquashfsMagicIsRaucBundle)
{
    std::string s(64, '\0');
    std::memcpy(&s[0], "hsqs", 4);
    EXPECT_EQ(fs::detect_update_format(probe_of(s)), fs::UpdateFormat::RaucBundle);
}

// --- the no-magic application image (version-field sniff; CRC is downstream) -

TEST(DetectUpdateFormat, AppImageVersionFieldIsApplicationImage)
{
    // app-image header: [0:8] big-endian size, [8:12] big-endian version == 1.
    // No magic at offset 0. CRC at [12:16] is NOT checked here (hybrid).
    std::string s(16, '\0');
    s[11] = 0x01; // big-endian version field at offset 8 == 1
    EXPECT_EQ(fs::detect_update_format(probe_of(s)), fs::UpdateFormat::ApplicationImage);
}

TEST(DetectUpdateFormat, NoMagicWrongVersionIsUnknown)
{
    std::string s(16, '\0');
    s[11] = 0x02; // version field == 2, not an application image
    EXPECT_EQ(fs::detect_update_format(probe_of(s)), fs::UpdateFormat::Unknown);
}

// --- short / empty / null probes are deterministically Unknown --------------

TEST(DetectUpdateFormat, EmptyProbeIsUnknown)
{
    EXPECT_EQ(fs::detect_update_format(probe_of("")), fs::UpdateFormat::Unknown);
}

TEST(DetectUpdateFormat, ShortProbeIsUnknown)
{
    // 8 bytes: too short for the version-field read at offset 8..11
    EXPECT_EQ(fs::detect_update_format(probe_of(std::string(8, '\0'))),
              fs::UpdateFormat::Unknown);
}

TEST(DetectUpdateFormat, NullDataIsUnknown)
{
    EXPECT_EQ(fs::detect_update_format(fs::FormatProbe{nullptr, 0}),
              fs::UpdateFormat::Unknown);
}

} // namespace
