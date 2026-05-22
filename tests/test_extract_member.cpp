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
#include <vector>

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

TEST(ExtractMember, OnChunkFiresWithMonotonicCumulativeBytesUnderOneChunk)
{
    /* Small payload (< kStreamChunk = 8192) — exactly one read iteration,
     * exactly one on_chunk call with the full size. */
    const std::string payload = "Hello, world!";
    std::istringstream src(payload, std::ios::binary);
    const auto m = make_member(0, payload.size(), sha256_of(payload));

    RecordingSink sink;
    std::vector<std::uint64_t> ticks;
    fs::extract_member(src, m, sink,
        [&ticks](std::uint64_t bytes_written) { ticks.push_back(bytes_written); });

    ASSERT_EQ(ticks.size(), 1u);
    EXPECT_EQ(ticks.back(), payload.size());
    EXPECT_EQ(sink.commit_calls, 1);
}

TEST(ExtractMember, OnChunkFiresMultipleTimesAcrossChunkBoundaries)
{
    /* Payload spans 3 reads: 8192 + 8192 + 100. on_chunk should fire
     * exactly 3 times with strictly-increasing values and final == size. */
    std::string payload(8192 * 2 + 100, 'x');
    std::istringstream src(payload, std::ios::binary);
    const auto m = make_member(0, payload.size(), sha256_of(payload));

    RecordingSink sink;
    std::vector<std::uint64_t> ticks;
    fs::extract_member(src, m, sink,
        [&ticks](std::uint64_t bytes_written) { ticks.push_back(bytes_written); });

    ASSERT_EQ(ticks.size(), 3u);
    EXPECT_EQ(ticks[0], 8192u);
    EXPECT_EQ(ticks[1], 16384u);
    EXPECT_EQ(ticks[2], payload.size());
    EXPECT_LT(ticks[0], ticks[1]);
    EXPECT_LT(ticks[1], ticks[2]);
    EXPECT_EQ(sink.commit_calls, 1);
}

TEST(ExtractMember, OnChunkNotFiredOnZeroSizeMember)
{
    /* Zero-size member: while loop body never runs, so on_chunk is
     * never called. The empty-string digest still matches and the sink
     * is committed. */
    std::istringstream src("", std::ios::binary);
    const auto m = make_member(0, 0, sha256_of(""));

    RecordingSink sink;
    std::vector<std::uint64_t> ticks;
    fs::extract_member(src, m, sink,
        [&ticks](std::uint64_t bytes_written) { ticks.push_back(bytes_written); });

    EXPECT_TRUE(ticks.empty());
    EXPECT_EQ(sink.commit_calls, 1);
}

TEST(ExtractMember, OnChunkNotFiredWhenFailureOccursBeforeFirstRead)
{
    /* Empty source, member claims >0 bytes → very first read returns 0
     * → truncation throw before on_chunk could fire. */
    std::istringstream src("", std::ios::binary);
    const auto m = make_member(0, /*claimed=*/100, sha256_of(""));

    RecordingSink sink;
    std::vector<std::uint64_t> ticks;
    EXPECT_THROW(
        fs::extract_member(src, m, sink,
            [&ticks](std::uint64_t bytes_written) { ticks.push_back(bytes_written); }),
        fs::GenericException);
    EXPECT_TRUE(ticks.empty());
    EXPECT_EQ(sink.abort_calls, 1);
}

TEST(ExtractMember, OnChunkFiresForSuccessfulChunksThenAbortsOnMidStreamFailure)
{
    /* Source has 5 real bytes but member claims 100 → first read
     * returns 5 (success, on_chunk fires with 5), second read returns 0
     * (EOF, truncation throw). on_chunk must have fired exactly once
     * for the successful chunk; sink must be aborted. */
    const std::string payload = "short";
    std::istringstream src(payload, std::ios::binary);
    const auto m = make_member(0, /*claimed=*/100, sha256_of(payload));

    RecordingSink sink;
    std::vector<std::uint64_t> ticks;
    EXPECT_THROW(
        fs::extract_member(src, m, sink,
            [&ticks](std::uint64_t bytes_written) { ticks.push_back(bytes_written); }),
        fs::GenericException);
    ASSERT_EQ(ticks.size(), 1u);
    EXPECT_EQ(ticks.back(), payload.size());
    EXPECT_EQ(sink.commit_calls, 0);
    EXPECT_EQ(sink.abort_calls, 1);
}

TEST(ExtractMember, NullOnChunkPreservesExistingBehavior)
{
    /* Defaulted nullptr arg must behave identically to the old
     * 3-arg signature: extraction succeeds, sink is committed, no
     * crash from invoking a null callback. */
    const std::string payload = "Hello, world!";
    std::istringstream src(payload, std::ios::binary);
    const auto m = make_member(0, payload.size(), sha256_of(payload));

    RecordingSink sink;
    fs::extract_member(src, m, sink, nullptr);

    EXPECT_EQ(sink.buffer, payload);
    EXPECT_EQ(sink.commit_calls, 1);
}
