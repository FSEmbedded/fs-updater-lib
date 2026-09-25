#include <gtest/gtest.h>

#include "handle_update/utils.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

// The predicate behind applicationUpdate::ensure_target_not_mounted(). The
// method itself needs the device's RAUC configuration and is not reachable
// here; its placement before the first environment write is proven by
// reading fsupdate.cpp.

namespace
{

class LoopBackingFile : public ::testing::Test
{
protected:
    std::filesystem::path file;

    void SetUp() override
    {
        file = std::filesystem::temp_directory_path() / ("fsup_loop_backing_" + std::to_string(::getpid()));
        std::filesystem::remove(file);
    }

    void TearDown() override { std::filesystem::remove(file); }

    void write(const std::string &content) { std::ofstream(file) << content; }
};

TEST_F(LoopBackingFile, NamesTheMountedImageOnly)
{
    write("/rw_fs/root/application/app_a.squashfs\n");
    EXPECT_TRUE(util::loop_backing_file_names(file, "app_a.squashfs"));
    EXPECT_FALSE(util::loop_backing_file_names(file, "app_b.squashfs"));
}

// A replaced backing file is reported with a suffix and still names the image.
TEST_F(LoopBackingFile, StillNamesAnImageThatWasReplacedUnderneath)
{
    write("/rw_fs/root/application/app_b.squashfs (deleted)\n");
    EXPECT_TRUE(util::loop_backing_file_names(file, "app_b.squashfs"));
    EXPECT_FALSE(util::loop_backing_file_names(file, "app_a.squashfs"));
}

TEST_F(LoopBackingFile, MissingLoopDeviceIsNoMatch)
{
    ASSERT_FALSE(std::filesystem::exists(file));
    EXPECT_FALSE(util::loop_backing_file_names(file, "app_a.squashfs"));
    EXPECT_FALSE(util::loop_backing_file_names(file, "app_b.squashfs"));
}

TEST_F(LoopBackingFile, EmptyBackingFileIsNoMatch)
{
    write("");
    EXPECT_FALSE(util::loop_backing_file_names(file, "app_a.squashfs"));
}

} // namespace
