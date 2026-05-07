#include <gtest/gtest.h>

#include "handle_update/UpdateStreamSink.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

std::string make_tmp_path(const char* tag)
{
    std::string p = "/tmp/fs-updater-lib-test-";
    p += tag;
    p += "-";
    p += std::to_string(::getpid());
    return p;
}

} // namespace

TEST(FileSink, WritesAllBytesToCommittedFile)
{
    const auto path = make_tmp_path("filesink-commit");
    {
        fs::FileSink sink(path);
        sink.write("hello", 5);
        sink.write(" world", 6);
        sink.commit();
    }
    EXPECT_EQ(slurp(path), "hello world");
    std::remove(path.c_str());
}

TEST(FileSink, AbortRemovesPartialFile)
{
    const auto path = make_tmp_path("filesink-abort");
    {
        fs::FileSink sink(path);
        sink.write("partial", 7);
        sink.abort();
    }
    std::ifstream f(path);
    EXPECT_FALSE(f.good()) << "aborted file should not exist at " << path;
}

TEST(FileSink, DestructorWithoutCommitAborts)
{
    const auto path = make_tmp_path("filesink-dtor");
    {
        fs::FileSink sink(path);
        sink.write("leaked", 6);
        // no commit() — destructor must clean up
    }
    std::ifstream f(path);
    EXPECT_FALSE(f.good()) << "uncommitted FileSink must remove its temp output";
}

TEST(DiscardSink, AcceptsWritesAndCommitsCleanly)
{
    fs::DiscardSink sink;
    sink.write("anything", 8);
    sink.write("else", 4);
    sink.commit();
    SUCCEED();
}

TEST(DiscardSink, AbortIsNoop)
{
    fs::DiscardSink sink;
    sink.write("x", 1);
    sink.abort();
    SUCCEED();
}
