#include <gtest/gtest.h>

#include "handle_update/sources/RaucBundleSource.h"
#include "handle_update/sources/UpdateSource.h"

#include <filesystem>
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

} // namespace
