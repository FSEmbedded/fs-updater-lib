#include <gtest/gtest.h>

#include "handle_update/Descriptor.h"
#include "handle_update/Sha256Hasher.h"
#include "handle_update/UpdateStoreV2.h"
#include "handle_update/UpdateStreamSink.h"
#include "handle_update/fs_exceptions.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace {

class RecordingSink : public fs::UpdateStreamSink
{
public:
    std::string buffer;
    int commit_calls = 0;
    int abort_calls = 0;

    void write(const char* d, std::size_t n) override { buffer.append(d, n); }
    void commit() override { ++commit_calls; }
    void abort() override { ++abort_calls; }
};

std::string sha256_of(std::string_view s)
{
    fs::Sha256Hasher h;
    h.update(s.data(), s.size());
    return h.hex_digest();
}

fs::Member make_member(uint64_t offset, uint64_t size, std::string_view sha256_hex,
                       std::string_view name = "x")
{
    fs::Member m;
    m.name = std::string(name);
    m.type = fs::MemberType::Application;
    m.offset = offset;
    m.size = size;
    m.sha256 = std::string(sha256_hex);
    return m;
}

} // namespace

TEST(ExtractMember, PipesAllBytesAndCommitsOnHashMatch)
{
    const std::string payload = "Hello, world!";
    std::istringstream src(payload, std::ios::binary);
    const auto m = make_member(0, payload.size(), sha256_of(payload));

    RecordingSink sink;
    fs::extract_member(src, m, sink);

    EXPECT_EQ(sink.buffer, payload);
    EXPECT_EQ(sink.commit_calls, 1);
    EXPECT_EQ(sink.abort_calls, 0);
}

TEST(ExtractMember, HonorsOffsetIntoSource)
{
    const std::string source_bytes = "PADDING-Actual payload-PADDING";
    const std::string payload = "Actual payload";
    std::istringstream src(source_bytes, std::ios::binary);
    const auto m = make_member(8, payload.size(), sha256_of(payload));

    RecordingSink sink;
    fs::extract_member(src, m, sink);

    EXPECT_EQ(sink.buffer, payload);
    EXPECT_EQ(sink.commit_calls, 1);
}

TEST(ExtractMember, ThrowsAndAbortsOnHashMismatch)
{
    const std::string payload = "Hello, world!";
    std::istringstream src(payload, std::ios::binary);
    const auto m = make_member(
        0, payload.size(),
        "0000000000000000000000000000000000000000000000000000000000000000");

    RecordingSink sink;
    EXPECT_THROW(fs::extract_member(src, m, sink), fs::GenericException);
    EXPECT_EQ(sink.commit_calls, 0);
    EXPECT_EQ(sink.abort_calls, 1);
}

TEST(ExtractMember, ThrowsAndAbortsOnSourceTruncation)
{
    const std::string payload = "short";
    std::istringstream src(payload, std::ios::binary);
    const auto m = make_member(0, /*claimed=*/100, sha256_of(payload));

    RecordingSink sink;
    EXPECT_THROW(fs::extract_member(src, m, sink), fs::GenericException);
    EXPECT_EQ(sink.commit_calls, 0);
    EXPECT_EQ(sink.abort_calls, 1);
}

TEST(ExtractMember, ZeroSizeMemberCommitsWithEmptyStringDigest)
{
    std::istringstream src("anything", std::ios::binary);
    const auto m = make_member(0, 0, sha256_of(""));

    RecordingSink sink;
    fs::extract_member(src, m, sink);

    EXPECT_EQ(sink.buffer, "");
    EXPECT_EQ(sink.commit_calls, 1);
    EXPECT_EQ(sink.abort_calls, 0);
}

TEST(ExtractMember, ZeroSizeMemberWithWrongHashStillThrows)
{
    std::istringstream src("anything", std::ios::binary);
    const auto m = make_member(
        0, 0, "0000000000000000000000000000000000000000000000000000000000000000");

    RecordingSink sink;
    EXPECT_THROW(fs::extract_member(src, m, sink), fs::GenericException);
    EXPECT_EQ(sink.abort_calls, 1);
}

TEST(ExtractMember, SequentialMembersOnSameSource)
{
    std::istringstream src("AAAAABBBBB", std::ios::binary);

    const auto a = make_member(0, 5, sha256_of("AAAAA"), "a");
    const auto b = make_member(5, 5, sha256_of("BBBBB"), "b");

    RecordingSink sink_a;
    fs::extract_member(src, a, sink_a);
    EXPECT_EQ(sink_a.buffer, "AAAAA");
    EXPECT_EQ(sink_a.commit_calls, 1);

    RecordingSink sink_b;
    fs::extract_member(src, b, sink_b);
    EXPECT_EQ(sink_b.buffer, "BBBBB");
    EXPECT_EQ(sink_b.commit_calls, 1);
}
