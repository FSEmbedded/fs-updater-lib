#include <gtest/gtest.h>

#include "handle_update/sources/UpdateContainerSource.h"
#include "handle_update/sources/UpdateSource.h"
#include "v2_test_helpers.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Build a single-member v2 container (type = "fw" | "app" | other) carrying
// `payload`, using the same fixed-point offset trick as the reader tests.
std::string make_single_member_container(const std::string& type, const std::string& payload)
{
    const std::string sha = fs_test::sha256_of(payload);

    auto build_descriptor = [&](std::uint64_t offset) {
        return std::string(R"json({"version":"2.0","members":[{"name":"m","type":")json")
             + type + R"json(","offset":)json" + std::to_string(offset)
             + R"json(,"size":)json" + std::to_string(payload.size())
             + R"json(,"sha256":")json" + sha + R"json("}]})json";
    };

    std::uint64_t offset = 200;
    std::string descriptor;
    for (int i = 0; i < 6; ++i) {
        descriptor = build_descriptor(offset);
        const std::uint64_t computed = 64u + 4u + descriptor.size();
        if (computed == offset) break;
        offset = computed;
    }
    const std::uint64_t descriptor_end = 64u + 4u + descriptor.size();
    const std::string padding(static_cast<std::size_t>(offset - descriptor_end), '\0');
    return fs_test::make_v2_stream(0x20, "FSUPv2", descriptor, padding + payload);
}

std::string read_file(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::string out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return out;
}

// RAII temp staging dir.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* tag)
        : path(std::filesystem::temp_directory_path() /
               (std::string("fsup-src-") + tag + "-" + std::to_string(::getpid()))) {
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

TEST(UpdateContainerSource, AppOnlyYieldsApplicationArtifact)
{
    const std::string payload = "APP-PAYLOAD";
    const std::string bytes   = make_single_member_container("app", payload);
    const std::string path    = fs_test::write_temp_file(bytes, "src-app");
    TempDir stage("app");

    fs::UpdateContainerSource src(path);
    const fs::UpdateArtifacts art = src.prepare(fs::StagingContext{stage.path, nullptr});

    ASSERT_TRUE(art.application.has_value());
    EXPECT_FALSE(art.firmware.has_value());
    EXPECT_EQ(read_file(*art.application), payload);

    std::remove(path.c_str());
}

TEST(UpdateContainerSource, FwOnlyYieldsFirmwareArtifact)
{
    const std::string payload = "FW-PAYLOAD-DATA";
    const std::string bytes   = make_single_member_container("firmware", payload);
    const std::string path    = fs_test::write_temp_file(bytes, "src-fw");
    TempDir stage("fw");

    fs::UpdateContainerSource src(path);
    const fs::UpdateArtifacts art = src.prepare(fs::StagingContext{stage.path, nullptr});

    ASSERT_TRUE(art.firmware.has_value());
    EXPECT_FALSE(art.application.has_value());
    EXPECT_EQ(read_file(*art.firmware), payload);

    std::remove(path.c_str());
}

} // namespace
