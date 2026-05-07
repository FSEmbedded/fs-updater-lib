#include <gtest/gtest.h>

#include "handle_update/Descriptor.h"
#include "handle_update/UpdateStoreV2.h"
#include "handle_update/fs_exceptions.h"
#include "v2_test_helpers.h"

#include <cstdint>
#include <cstdio>
#include <string>

using fs_test::make_v2_stream;
using fs_test::RecordingSink;
using fs_test::sha256_of;
using fs_test::write_temp_file;

TEST(V2ContainerReader, OpenReadsHeaderAndDescriptorFromFile)
{
    constexpr const char* desc = R"json({
        "version": "2.0",
        "members": [{
            "name": "x",
            "type": "firmware",
            "offset": 0,
            "size": 0,
            "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        }]
    })json";

    const auto bytes = make_v2_stream(0x20, "FSUPv2", desc, "");
    const auto path = write_temp_file(bytes, "v2reader-open");

    fs::V2ContainerReader reader(path);
    reader.open();

    EXPECT_EQ(reader.descriptor().version, "2.0");
    ASSERT_EQ(reader.descriptor().members.size(), 1u);
    EXPECT_EQ(reader.descriptor().members[0].name, "x");

    std::remove(path.c_str());
}

TEST(V2ContainerReader, ExtractRoundTripsARealMember)
{
    const std::string payload = "HELLO";
    const std::string sha = sha256_of(payload);

    auto build_descriptor = [&](std::uint64_t offset) {
        return std::string(R"json({"version":"2.0","members":[{"name":"x","type":"app","offset":)json")
             + std::to_string(offset)
             + R"json(,"size":5,"sha256":")json"
             + sha + R"json("}]})json";
    };

    // The descriptor's offset string-width affects descriptor size, so
    // iterate to fixed-point on `offset = 64 + 4 + descriptor.size()`.
    std::uint64_t offset = 200;
    std::string descriptor;
    for (int i = 0; i < 5; ++i) {
        descriptor = build_descriptor(offset);
        const std::uint64_t computed = 64u + 4u + descriptor.size();
        if (computed == offset) {
            break;
        }
        offset = computed;
    }

    // Pad zero bytes between descriptor and the actual member payload so
    // the on-disk member begins at exactly `offset`.
    const std::uint64_t descriptor_end = 64u + 4u + descriptor.size();
    ASSERT_LE(descriptor_end, offset);
    const std::string padding(static_cast<std::size_t>(offset - descriptor_end), '\0');
    const std::string payload_region = padding + payload;

    const auto bytes = make_v2_stream(0x20, "FSUPv2", descriptor, payload_region);
    const auto path = write_temp_file(bytes, "v2reader-extract");

    fs::V2ContainerReader reader(path);
    reader.open();
    ASSERT_EQ(reader.descriptor().members.size(), 1u);

    RecordingSink sink;
    reader.extract(reader.descriptor().members[0], sink);
    EXPECT_EQ(sink.buffer, payload);
    EXPECT_EQ(sink.commit_calls, 1);
    EXPECT_EQ(sink.abort_calls, 0);

    std::remove(path.c_str());
}

TEST(V2ContainerReader, OpenThrowsOnNonExistentPath)
{
    fs::V2ContainerReader reader("/tmp/fs-updater-lib-no-such-file-XYZ-12345.fs");
    EXPECT_THROW(reader.open(), fs::GenericException);
}

TEST(V2ContainerReader, OpenRejectsLegacyV10File)
{
    const auto bytes = make_v2_stream(0x10, "CERT", "", "");
    const auto path = write_temp_file(bytes, "v2reader-v1");

    fs::V2ContainerReader reader(path);
    EXPECT_THROW(reader.open(), fs::GenericException);

    std::remove(path.c_str());
}

TEST(V2ContainerReader, DescriptorBeforeOpenThrows)
{
    fs::V2ContainerReader reader("/tmp/whatever.fs");
    EXPECT_THROW((void)reader.descriptor(), fs::GenericException);
}
