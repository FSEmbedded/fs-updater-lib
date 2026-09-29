#include <gtest/gtest.h>

#include "handle_update/utils.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

// The predicate behind FSUpdate::update_image()'s refusal of an update file
// that the emptying of the extraction directory would delete. The call site
// itself is not host-buildable.

namespace
{

class PathIsInside : public ::testing::Test
{
protected:
    std::filesystem::path base;
    std::filesystem::path dir;
    std::filesystem::path out;
    std::error_code ec;

    void SetUp() override
    {
        base = std::filesystem::temp_directory_path() / ("fsup_inside_" + std::to_string(::getpid()));
        std::filesystem::remove_all(base);
        dir = base / "dir";
        out = base / "out";
        std::filesystem::create_directories(dir);
        std::filesystem::create_directories(out);
        std::ofstream(dir / "f") << "";
        std::ofstream(out / "f") << "";
    }

    void TearDown() override { std::filesystem::remove_all(base); }

    bool inside(const std::filesystem::path &path) { return util::path_is_inside(path, dir, ec); }
};

TEST_F(PathIsInside, AFileInTheDirectory)
{
    EXPECT_TRUE(inside(dir / "f"));
    EXPECT_FALSE(ec);
}

TEST_F(PathIsInside, DotDotIsResolved)
{
    EXPECT_TRUE(inside(dir / "sub" / ".." / "f"));
    EXPECT_FALSE(inside(dir / ".." / "out" / "f"));
    EXPECT_FALSE(ec);
}

TEST_F(PathIsInside, ASymlinkNamingAFileInside)
{
    std::filesystem::create_symlink(dir / "f", out / "link");
    EXPECT_TRUE(inside(out / "link"));
    EXPECT_FALSE(ec);
}

TEST_F(PathIsInside, APathThroughASymlinkedParent)
{
    std::filesystem::create_directory_symlink(dir, base / "dirlink");
    EXPECT_TRUE(inside(base / "dirlink" / "f"));
    EXPECT_FALSE(ec);
}

// As when a parent of the extraction directory is itself a symlink.
TEST_F(PathIsInside, TheDirectoryNamedThroughASymlink)
{
    std::filesystem::create_directory_symlink(dir, base / "alias");
    EXPECT_TRUE(util::path_is_inside(dir / "f", base / "alias", ec));
    EXPECT_FALSE(ec);
}

// The link's own entry is what the emptying deletes, wherever it points.
TEST_F(PathIsInside, ASymlinkInsideNamingAFileOutside)
{
    std::filesystem::create_symlink(out / "f", dir / "link");
    EXPECT_TRUE(inside(dir / "link"));
    EXPECT_FALSE(ec);
}

TEST_F(PathIsInside, NeighboursAndOutsidePathsAreNot)
{
    std::filesystem::create_directories(base / "dir2");
    std::ofstream(base / "dir2" / "f") << "";
    EXPECT_FALSE(inside(base / "dir2" / "f"));
    EXPECT_FALSE(inside(out / "f"));
    EXPECT_FALSE(ec);
}

TEST_F(PathIsInside, TheDirectoryItselfIsNot)
{
    EXPECT_FALSE(inside(dir));
    EXPECT_FALSE(ec);
}

TEST_F(PathIsInside, RelativePaths)
{
    EXPECT_TRUE(inside(std::filesystem::relative(dir / "f")));
    EXPECT_FALSE(inside("fsup_no_such_dir_" + std::to_string(::getpid()) + "/f"));
    EXPECT_FALSE(ec);
}

// A path that cannot be resolved must not read as outside.
TEST_F(PathIsInside, AnUnresolvablePathReportsTheError)
{
    std::filesystem::create_symlink("loop", out / "loop");
    EXPECT_FALSE(inside(out / "loop" / "f"));
    EXPECT_EQ(ec.value(), ELOOP);
}

} // namespace
