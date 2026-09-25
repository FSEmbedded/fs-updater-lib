#include <gtest/gtest.h>

#include "handle_update/utils.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

std::filesystem::path make_temp_dir(const std::string &name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("fsup_scoped_" + name + "_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

TEST(ScopedDirectory, RemovesTreeIncludingContent)
{
    const std::filesystem::path dir = make_temp_dir("content");
    std::filesystem::create_directories(dir / "sub");
    std::ofstream(dir / "sub" / "payload") << "content";
    ASSERT_TRUE(std::filesystem::exists(dir / "sub" / "payload"));

    {
        util::ScopedDirectory guard(dir);
        EXPECT_EQ(guard.path(), dir);
        EXPECT_TRUE(std::filesystem::exists(dir));
    }

    EXPECT_FALSE(std::filesystem::exists(dir));
}

TEST(ScopedDirectory, RemovesTreeWhileAnExceptionUnwinds)
{
    const std::filesystem::path dir = make_temp_dir("unwind");
    std::ofstream(dir / "payload") << "content";

    try
    {
        util::ScopedDirectory guard(dir);
        throw std::runtime_error("install failed");
    }
    catch (const std::runtime_error &)
    {
    }

    EXPECT_FALSE(std::filesystem::exists(dir));
}

TEST(ScopedDirectory, DoesNotThrowOnAMissingPath)
{
    const std::filesystem::path dir = make_temp_dir("missing");
    std::filesystem::remove_all(dir);
    ASSERT_FALSE(std::filesystem::exists(dir));

    EXPECT_NO_THROW({ util::ScopedDirectory guard(dir); });
}

} // namespace
