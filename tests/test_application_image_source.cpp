#include <gtest/gtest.h>

#include "handle_update/sources/ApplicationImageSource.h"
#include "handle_update/sources/UpdateSource.h"

#include <filesystem>

namespace {

// A raw F&S application image is the application payload: prepare() hands the
// original path straight to the application engine (update_application →
// applicationImage), which validates the header/CRC itself. No staging copy.
TEST(ApplicationImageSource, PrepareYieldsApplicationAtOriginalPath)
{
    const std::filesystem::path image = "/tmp/some-upload/app.fs";
    fs::ApplicationImageSource src(image);

    const fs::UpdateArtifacts art = src.prepare(fs::StagingContext{"/unused/staging", nullptr});

    ASSERT_TRUE(art.application.has_value());
    EXPECT_EQ(*art.application, image);
    EXPECT_FALSE(art.firmware.has_value());
}

} // namespace
