#include <gtest/gtest.h>

#include "handle_update/RaucInstallSink.h"
#include "handle_update/fs_exceptions.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

std::string temp_scratch_path(const char* tag)
{
    std::string p = "/tmp/fs-updater-lib-test-rauc-";
    p += tag;
    p += "-";
    p += std::to_string(::getpid());
    return p;
}

bool file_exists(const std::string& p)
{
    return std::filesystem::exists(p);
}

} // namespace

TEST(RaucInstallSink, WritesBytesAndCommitsRemovesScratchOnRaucSuccess)
{
    const auto path = temp_scratch_path("commit-success");
    bool invoker_called = false;
    std::filesystem::path invoker_arg;

    fs::RaucInstallSink sink(path,
        [&](const std::filesystem::path& p) -> int {
            invoker_called = true;
            invoker_arg = p;
            // The scratch file should be at the expected path with our bytes
            std::ifstream f(p, std::ios::binary);
            const std::string content((std::istreambuf_iterator<char>(f)), {});
            EXPECT_EQ(content, "hello");
            return 0;
        });

    sink.write("hello", 5);
    sink.commit();

    EXPECT_TRUE(invoker_called);
    EXPECT_EQ(invoker_arg.string(), path);
    EXPECT_FALSE(file_exists(path)) << "scratch file must be removed on RAUC success";
}

TEST(RaucInstallSink, RaucFailureThrowsAndKeepsScratchForForensics)
{
    const auto path = temp_scratch_path("commit-failure");

    fs::RaucInstallSink sink(path,
        [](const std::filesystem::path&) -> int {
            return 42;  // non-zero = RAUC failure
        });

    sink.write("forensics", 9);
    EXPECT_THROW(sink.commit(), fs::GenericException);

    EXPECT_TRUE(file_exists(path))
        << "scratch file must be kept on RAUC failure for forensics";

    std::remove(path.c_str());
}

TEST(RaucInstallSink, AbortRemovesScratchAndNeverInvokesRauc)
{
    const auto path = temp_scratch_path("abort");
    bool invoker_called = false;

    fs::RaucInstallSink sink(path,
        [&](const std::filesystem::path&) -> int {
            invoker_called = true;
            return 0;
        });

    sink.write("partial", 7);
    sink.abort();

    EXPECT_FALSE(invoker_called);
    EXPECT_FALSE(file_exists(path));
    EXPECT_FALSE(file_exists(path + ".tmp"));
}

TEST(RaucInstallSink, DestructorWithoutCommitCleansUp)
{
    const auto path = temp_scratch_path("dtor");
    {
        fs::RaucInstallSink sink(path,
            [](const std::filesystem::path&) -> int {
                return 0;
            });
        sink.write("dropped", 7);
        // No commit, no abort — destructor should clean up via FileSink's dtor
    }
    EXPECT_FALSE(file_exists(path));
    EXPECT_FALSE(file_exists(path + ".tmp"));
}

TEST(RaucInstallSink, DefaultConstructorBindsRealRaucInvoker)
{
    // Just verify the single-arg ctor works without invoking RAUC.
    // We don't call commit() because that would shell out to /usr/bin/rauc.
    const auto path = temp_scratch_path("default-ctor");
    fs::RaucInstallSink sink(path);
    sink.abort();
    EXPECT_FALSE(file_exists(path));
}
