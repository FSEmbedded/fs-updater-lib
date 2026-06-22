#include "util/posix_utils.h"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <utility>

namespace {

using fs::util::DirGuard;
using fs::util::FdGuard;
using fs::util::ScopeGuard;

// Whether a file descriptor is still open.
bool fd_open(int fd)
{
    return ::fcntl(fd, F_GETFD) != -1;
}

TEST(ScopeGuard, RunsActionOnDestruction)
{
    bool ran = false;
    {
        ScopeGuard guard([&ran] { ran = true; });
        EXPECT_FALSE(ran);
    }
    EXPECT_TRUE(ran);
}

TEST(ScopeGuard, DismissPreventsAction)
{
    bool ran = false;
    {
        ScopeGuard guard([&ran] { ran = true; });
        guard.dismiss();
    }
    EXPECT_FALSE(ran);
}

TEST(FdGuard, ClosesOnDestruction)
{
    const int fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    EXPECT_TRUE(fd_open(fd));
    {
        FdGuard guard(fd);
        EXPECT_TRUE(guard.valid());
        EXPECT_EQ(guard.get(), fd);
    }
    EXPECT_FALSE(fd_open(fd)); // closed by the guard
}

TEST(FdGuard, DefaultConstructedIsInvalid)
{
    const FdGuard guard;
    EXPECT_FALSE(guard.valid());
    EXPECT_EQ(guard.get(), -1);
}

TEST(FdGuard, ReleaseTransfersOwnership)
{
    const int fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        FdGuard guard(fd);
        const int released = guard.release();
        EXPECT_EQ(released, fd);
        EXPECT_FALSE(guard.valid());
    }
    EXPECT_TRUE(fd_open(fd)); // guard released it, so still open
    ::close(fd);
}

TEST(FdGuard, MoveTransfersOwnership)
{
    const int fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        FdGuard a(fd);
        FdGuard b(std::move(a));
        EXPECT_FALSE(a.valid()); // NOLINT(bugprone-use-after-move) — intentional
        EXPECT_TRUE(b.valid());
        EXPECT_EQ(b.get(), fd);
    }
    EXPECT_FALSE(fd_open(fd)); // closed exactly once by b
}

TEST(FdGuard, ResetClosesPrevious)
{
    const int fd1 = ::open("/dev/null", O_RDONLY);
    const int fd2 = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);
    FdGuard guard(fd1);
    guard.reset(fd2);
    EXPECT_FALSE(fd_open(fd1)); // fd1 closed by reset
    EXPECT_EQ(guard.get(), fd2);
    // guard destructor closes fd2
}

TEST(DirGuard, OwnsAndClosesHandle)
{
    DIR *d = ::opendir(".");
    ASSERT_NE(d, nullptr);
    {
        const DirGuard guard(d);
        EXPECT_TRUE(guard.valid());
        EXPECT_EQ(guard.get(), d);
    }
    // closedir() invoked by the guard; no leak / double-close.
}

// --- POSIX path helpers ---

TEST(PosixUtils, MkdirPCreatesNestedDirectories)
{
    const std::string base = "/tmp/fsup_posix_test_" + std::to_string(::getpid());
    const std::string nested = base + "/a/b/c";
    ASSERT_TRUE(fs::util::mkdir_p(nested));
    EXPECT_TRUE(fs::util::path_exists(nested));
    EXPECT_TRUE(fs::util::is_directory(nested));
    EXPECT_TRUE(fs::util::mkdir_p(nested)); // idempotent
    ::rmdir(nested.c_str());
    ::rmdir((base + "/a/b").c_str());
    ::rmdir((base + "/a").c_str());
    ::rmdir(base.c_str());
}

TEST(PosixUtils, FileSizeThenRemove)
{
    const std::string f = "/tmp/fsup_posix_size_" + std::to_string(::getpid());
    const int fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::write(fd, "hello", 5), 5);
    ::close(fd);
    const auto sz = fs::util::file_size(f);
    ASSERT_TRUE(sz.has_value());
    EXPECT_EQ(*sz, 5U);
    EXPECT_TRUE(fs::util::remove_file(f));
    EXPECT_FALSE(fs::util::path_exists(f));
    EXPECT_FALSE(fs::util::remove_file(f)); // already gone
}

TEST(PosixUtils, FileSizeMissingIsNullopt)
{
    EXPECT_FALSE(fs::util::file_size("/tmp/fsup_posix_absent_zzz").has_value());
}

TEST(PosixUtils, SetPermissions)
{
    const std::string f = "/tmp/fsup_posix_perm_" + std::to_string(::getpid());
    const int fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    ::close(fd);
    EXPECT_TRUE(fs::util::set_permissions(f, 0600));
    struct stat st{};
    ASSERT_EQ(::stat(f.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600U);
    ::unlink(f.c_str());
}

TEST(PosixUtils, ParentPath)
{
    EXPECT_EQ(fs::util::parent_path("/a/b/c"), "/a/b");
    EXPECT_EQ(fs::util::parent_path("/a"), "/");
    EXPECT_EQ(fs::util::parent_path("a"), "");
    EXPECT_EQ(fs::util::parent_path("/a/b/"), "/a/b");
}

} // namespace
