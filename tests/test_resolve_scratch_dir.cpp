#include <string>

#include <gtest/gtest.h>

// std::filesystem is used here ONLY as the oracle the helper must reproduce,
// proving the <filesystem>-free implementation is byte-identical for these
// inputs. (Test code is not subject to the production banned-header rule.)
#include <filesystem>

#include "handle_update/fs_consts.h"
#include "handle_update/scratch_path.h"

namespace {
// The std::filesystem::parent_path() result, as a plain string.
std::string fs_parent(const std::string &p)
{
    return std::filesystem::path(p).parent_path().string();
}
} // namespace

TEST(ResolveScratchDir, EmptyOverrideUsesDefault)
{
    EXPECT_EQ(fs::resolve_scratch_dir(""), fs_parent(fs::DEFAULT_RAUC_SCRATCH_PATH));
}

TEST(ResolveScratchDir, FlatOverridePathUsesParent)
{
    EXPECT_EQ(fs::resolve_scratch_dir("/run/scratch.fw"), fs_parent("/run/scratch.fw"));
}

TEST(ResolveScratchDir, NestedOverridePathUsesParent)
{
    EXPECT_EQ(fs::resolve_scratch_dir("/run/sub/dir/scratch.fw"), fs_parent("/run/sub/dir/scratch.fw"));
}

TEST(ResolveScratchDir, OverrideWithTrailingSlashKeepsDir)
{
    // parent_path() of "/run/scratch/" is "/run/scratch" — documents the
    // behavior callers depend on.
    EXPECT_EQ(fs::resolve_scratch_dir("/run/scratch/"), fs_parent("/run/scratch/"));
}
