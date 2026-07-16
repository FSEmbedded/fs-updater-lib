#include <gtest/gtest.h>

#include "handle_update/sources/RaucBundleSource.h"
#include "handle_update/sources/UpdateSource.h"
#include "handle_update/fs_exceptions.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

// A raw RAUC bundle is firmware: prepare() hands the original path straight
// to the firmware engine (rauc install <bundle>) — no staging copy. The
// downstream applicationImage/RAUC layer validates the bundle itself.
TEST(RaucBundleSource, PrepareYieldsFirmwareAtOriginalPath)
{
    const std::filesystem::path bundle = "/tmp/some-upload/firmware.raucb";
    fs::RaucBundleSource src(bundle);

    const fs::UpdateArtifacts art = src.prepare(fs::StagingContext{"/unused/staging", nullptr});

    ASSERT_TRUE(art.firmware.has_value());
    EXPECT_EQ(*art.firmware, bundle);
    EXPECT_FALSE(art.application.has_value());
}

// Firmware and application bundles are both squashfs, so the byte sniff
// cannot separate them; the manifest `compatible` string is the
// discriminator. The "-appfs" suffix is the bundle-recipe convention.
TEST(RaucBundleSource, CompatibleSuffixClassifiesAppBundles)
{
    EXPECT_TRUE(fs::is_rauc_app_compatible("fus-update-board-appfs"));
    EXPECT_TRUE(fs::is_rauc_app_compatible("-appfs"));
    EXPECT_FALSE(fs::is_rauc_app_compatible("fus-update-board"));
    EXPECT_FALSE(fs::is_rauc_app_compatible("appfs"));
    EXPECT_FALSE(fs::is_rauc_app_compatible(""));
    EXPECT_FALSE(fs::is_rauc_app_compatible("fus-appfs-update"));
}

TEST(RaucBundleSource, PrepareWithAppCompatibleYieldsApplicationAtOriginalPath)
{
    const std::string bundle = "/tmp/some-upload/app.raucb";
    std::string seen_path;

    fs::RaucBundleSource src(bundle, [&](const std::string& p) {
        seen_path = p;
        return std::string("fus-update-board-appfs");
    });

    const fs::UpdateArtifacts art = src.prepare(fs::StagingContext{"/unused/staging", nullptr});

    EXPECT_EQ(seen_path, bundle);
    ASSERT_TRUE(art.application.has_value());
    EXPECT_EQ(*art.application, bundle);
    EXPECT_FALSE(art.firmware.has_value());
}

TEST(RaucBundleSource, PrepareWithNonAppCompatibleYieldsFirmware)
{
    const std::string bundle = "/tmp/some-upload/firmware.raucb";

    fs::RaucBundleSource src(bundle, [](const std::string&) {
        return std::string("fus-update-board");
    });

    const fs::UpdateArtifacts art = src.prepare(fs::StagingContext{"/unused/staging", nullptr});

    ASSERT_TRUE(art.firmware.has_value());
    EXPECT_EQ(*art.firmware, bundle);
    EXPECT_FALSE(art.application.has_value());
}

// The provider talks D-Bus underneath (Rauc* exceptions derive from
// std::exception, not GenericException); prepare() must keep its own
// "throws fs::GenericException" contract by wrapping provider failures.
TEST(RaucBundleSource, PrepareWrapsProviderFailureInGenericException)
{
    fs::RaucBundleSource src("/tmp/some-upload/any.raucb", [](const std::string&) -> std::string {
        throw std::runtime_error("rauc service unavailable");
    });

    EXPECT_THROW((void)src.prepare(fs::StagingContext{"/unused/staging", nullptr}),
                 fs::GenericException);
}

} // namespace
