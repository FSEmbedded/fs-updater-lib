#include <gtest/gtest.h>

#include "handle_update/sources/ApplicationImageSource.h"
#include "handle_update/sources/RaucBundleSource.h"
#include "handle_update/sources/UpdateContainerSource.h"
#include "handle_update/sources/UpdateSourceRegistry.h"
#include "handle_update/fs_exceptions.h"
#include "v2_test_helpers.h"

#include <cstring>
#include <memory>
#include <string>

namespace {

TEST(MakeUpdateSource, ContainerYieldsContainerSource)
{
    const std::string bytes = fs_test::make_v2_stream(0x20, "FSUPv2", "{}", "");
    const std::string path  = fs_test::write_temp_file(bytes, "mksrc-container");

    auto src = fs::make_update_source(path);
    ASSERT_NE(src, nullptr);
    EXPECT_NE(dynamic_cast<fs::UpdateContainerSource*>(src.get()), nullptr);

    std::remove(path.c_str());
}

TEST(MakeUpdateSource, GarbageThrowsUnknownUpdateFormat)
{
    const std::string bytes(64, '\xAB'); // no FSLX/hsqs, version field != 1
    const std::string path = fs_test::write_temp_file(bytes, "mksrc-garbage");

    EXPECT_THROW((void)fs::make_update_source(path), fs::UnknownUpdateFormat);

    std::remove(path.c_str());
}

TEST(MakeUpdateSource, RaucBundleYieldsRaucBundleSource)
{
    std::string bytes(64, '\0');
    std::memcpy(&bytes[0], "hsqs", 4); // squashfs magic -> RaucBundle
    const std::string path = fs_test::write_temp_file(bytes, "mksrc-rauc");

    auto src = fs::make_update_source(path);
    ASSERT_NE(src, nullptr);
    EXPECT_NE(dynamic_cast<fs::RaucBundleSource*>(src.get()), nullptr);

    std::remove(path.c_str());
}

TEST(MakeUpdateSource, ApplicationImageYieldsApplicationImageSource)
{
    std::string bytes(64, '\0');
    bytes[11] = 0x01; // big-endian version field @8 == 1 -> ApplicationImage
    const std::string path = fs_test::write_temp_file(bytes, "mksrc-app");

    auto src = fs::make_update_source(path);
    ASSERT_NE(src, nullptr);
    EXPECT_NE(dynamic_cast<fs::ApplicationImageSource*>(src.get()), nullptr);

    std::remove(path.c_str());
}

// The factory forwards the manifest-compatible provider to the RAUC source,
// so an auto-detected app bundle resolves to an application artifact.
TEST(MakeUpdateSource, RaucBundleForwardsCompatibleProvider)
{
    std::string bytes(64, '\0');
    std::memcpy(&bytes[0], "hsqs", 4); // squashfs magic -> RaucBundle
    const std::string path = fs_test::write_temp_file(bytes, "mksrc-rauc-app");

    auto src = fs::make_update_source(path, [](const std::string&) {
        return std::string("fus-update-board-appfs");
    });
    ASSERT_NE(src, nullptr);

    const fs::UpdateArtifacts art = src->prepare(fs::StagingContext{"/unused/staging", nullptr});
    ASSERT_TRUE(art.application.has_value());
    EXPECT_EQ(*art.application, path);
    EXPECT_FALSE(art.firmware.has_value());

    std::remove(path.c_str());
}

TEST(MakeUpdateSource, NonExistentPathThrows)
{
    EXPECT_THROW((void)fs::make_update_source("/tmp/fs-updater-no-such-file-XYZ-987.bin"),
                 fs::GenericException);
}

} // namespace
