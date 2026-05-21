#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "handle_update/fs_consts.h"
#include "handle_update/scratch_path.h"

TEST(ResolveScratchDir, EmptyOverrideUsesDefault)
{
    const std::filesystem::path expected =
        std::filesystem::path(fs::DEFAULT_RAUC_SCRATCH_PATH).parent_path();
    EXPECT_EQ(fs::resolve_scratch_dir(""), expected);
}

TEST(ResolveScratchDir, FlatOverridePathUsesParent)
{
    EXPECT_EQ(fs::resolve_scratch_dir("/run/scratch.fw"),
              std::filesystem::path("/run"));
}

TEST(ResolveScratchDir, NestedOverridePathUsesParent)
{
    EXPECT_EQ(fs::resolve_scratch_dir("/run/sub/dir/scratch.fw"),
              std::filesystem::path("/run/sub/dir"));
}

TEST(ResolveScratchDir, OverrideWithTrailingSlashKeepsDir)
{
    // parent_path() of "/run/scratch/" is "/run/scratch" — documents
    // the std::filesystem::path behavior callers depend on.
    EXPECT_EQ(fs::resolve_scratch_dir("/run/scratch/"),
              std::filesystem::path("/run/scratch"));
}
