#include "util/posix_utils.h"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

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

} // namespace
