#include <gtest/gtest.h>

#include "handle_update/applicationImage.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkStdout.h"

extern "C" {
#include <zlib.h>
#include <unistd.h>
}

#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

// On-disk layout under test (all header fields big-endian):
//   [8B image size][4B header version][4B crc32 over the first 12 bytes]
//   [image content][26B ISO timestamp][signature][optional PEM certs]

constexpr size_t kHeaderSize = 16;
constexpr size_t kTimestampSize = 26;

std::shared_ptr<logger::LoggerHandler> test_logger()
{
    static auto handler = logger::LoggerHandler::initLogger(
        std::make_shared<logger::LoggerSinkStdout>(logger::logLevel::ERROR));
    return handler;
}

void put_be64(std::vector<uint8_t>& out, uint64_t v)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
    }
}

void put_be32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
    }
}

std::vector<uint8_t> build_header(uint64_t declared_size, uint32_t version,
                                  bool corrupt_crc = false)
{
    std::vector<uint8_t> header;
    put_be64(header, declared_size);
    put_be32(header, version);
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, header.data(), 12);
    if (corrupt_crc) {
        crc ^= 0xDEADBEEF;
    }
    put_be32(header, crc);
    return header;
}

// Self-deleting temp file holding an assembled image.
class TempImage {
  public:
    explicit TempImage(const std::vector<uint8_t>& bytes)
    {
        char tmpl[] = "/tmp/appimg-test-XXXXXX";
        fd_ = mkstemp(tmpl);
        EXPECT_GE(fd_, 0);
        path_ = tmpl;
        EXPECT_EQ(write(fd_, bytes.data(), bytes.size()),
                  static_cast<ssize_t>(bytes.size()));
    }
    ~TempImage()
    {
        if (fd_ >= 0) {
            close(fd_);
        }
        unlink(path_.c_str());
    }
    [[nodiscard]] const std::string& path() const { return path_; }

  private:
    int fd_ = -1;
    std::string path_;
};

std::vector<uint8_t> assemble(uint64_t declared_size, uint32_t version,
                              const std::string& content,
                              const std::string& timestamp,
                              const std::string& signature,
                              bool corrupt_crc = false,
                              const std::string& trailer = "")
{
    std::vector<uint8_t> bytes = build_header(declared_size, version, corrupt_crc);
    auto append = [&bytes](const std::string& s) {
        bytes.insert(bytes.end(), s.begin(), s.end());
    };
    append(content);
    std::string ts = timestamp;
    ts.resize(kTimestampSize, '\0');
    append(ts);
    append(signature);
    append(trailer);
    return bytes;
}

// The parser rejects are asserted through this single mapping, kept as the
// one place to update; which reject fires for which corruption is the
// pinned contract.
enum class Reject {
    none,
    open,
    too_small,
    wrong_version,
    wrong_crc,
    bad_timestamp,
    write_error,
    other,
};

Reject reject_of(const std::function<void()>& fn)
{
    try {
        fn();
        return Reject::none;
    } catch (const WrongHeaderVersion&) {
        return Reject::wrong_version;
    } catch (const WrongHeaderChecksum&) {
        return Reject::wrong_crc;
    } catch (const ImageUpdatePackageToSmall&) {
        return Reject::too_small;
    } catch (const ReadPointOfTime&) {
        return Reject::bad_timestamp;
    } catch (const DuringWriteApplicationImage&) {
        return Reject::write_error;
    } catch (const OpenApplicationImage&) {
        return Reject::open;
    } catch (const std::exception&) {
        return Reject::other;
    }
}

const std::string kContent = "SQUASHFS-PAYLOAD-0123456789abcdef-SQUASHFS-PAYLOAD-0123456789ab";
const std::string kTimestamp = "2026-07-05T12:34:56Z";
const std::string kSignature(256, '\x5a');

TEST(ApplicationImageParse, ValidImageParses)
{
    TempImage img(assemble(kContent.size(), 1, kContent, kTimestamp, kSignature));
    applicationImage app(img.path(), test_logger());
    EXPECT_EQ(app.getSizeOfImage(), kContent.size());
    EXPECT_EQ(app.getPath(), img.path());
    EXPECT_EQ(app.getHeader().size(), kHeaderSize);
    EXPECT_EQ(app.getTimestamp().size(), kTimestampSize);
}

TEST(ApplicationImageParse, NonexistentPathRejected)
{
    EXPECT_EQ(reject_of([] {
                  applicationImage app("/nonexistent/appimg", test_logger());
              }),
              Reject::other);
}

TEST(ApplicationImageParse, HeaderOnlyFileTooSmall)
{
    TempImage img(build_header(0, 1));
    EXPECT_EQ(reject_of([&] { applicationImage app(img.path(), test_logger()); }),
              Reject::too_small);
}

TEST(ApplicationImageParse, TruncatedHeaderTooSmall)
{
    std::vector<uint8_t> ten_bytes(10, 0x42);
    TempImage img(ten_bytes);
    EXPECT_EQ(reject_of([&] { applicationImage app(img.path(), test_logger()); }),
              Reject::too_small);
}

TEST(ApplicationImageParse, WrongHeaderVersionRejected)
{
    for (uint32_t version : {0U, 2U, 0xFFFFFFFFU}) {
        TempImage img(assemble(kContent.size(), version, kContent, kTimestamp, kSignature));
        EXPECT_EQ(reject_of([&] { applicationImage app(img.path(), test_logger()); }),
                  Reject::wrong_version)
            << "version=" << version;
    }
}

TEST(ApplicationImageParse, CorruptCrcRejected)
{
    TempImage img(assemble(kContent.size(), 1, kContent, kTimestamp, kSignature,
                           /*corrupt_crc=*/true));
    EXPECT_EQ(reject_of([&] { applicationImage app(img.path(), test_logger()); }),
              Reject::wrong_crc);
}

TEST(ApplicationImageParse, VersionCheckedBeforeCrc)
{
    TempImage img(assemble(kContent.size(), 7, kContent, kTimestamp, kSignature,
                           /*corrupt_crc=*/true));
    EXPECT_EQ(reject_of([&] { applicationImage app(img.path(), test_logger()); }),
              Reject::wrong_version);
}

TEST(ApplicationImageTimestamp, ValidTimestampParses)
{
    TempImage img(assemble(kContent.size(), 1, kContent, kTimestamp, kSignature));
    applicationImage app(img.path(), test_logger());
    EXPECT_NO_THROW({
        auto tp = app.getTimeOfSigning();
        EXPECT_NE(tp.time_since_epoch().count(), 0);
    });
}

TEST(ApplicationImageTimestamp, GarbageTimestampRejected)
{
    TempImage img(assemble(kContent.size(), 1, kContent, "not-a-timestamp!!", kSignature));
    applicationImage app(img.path(), test_logger());
    EXPECT_EQ(reject_of([&] { app.getTimeOfSigning(); }), Reject::bad_timestamp);
}

TEST(ApplicationImageTimestamp, OversizedDeclaredSizeHitsEof)
{
    // Header lies: declared size points past the end of the file, so the
    // timestamp read must fail instead of returning attacker-chosen bytes.
    TempImage img(assemble(4096, 1, kContent, kTimestamp, kSignature));
    applicationImage app(img.path(), test_logger());
    EXPECT_EQ(reject_of([&] { app.getTimeOfSigning(); }), Reject::open);
}

TEST(ApplicationImageSignature, SignatureRoundTrips)
{
    TempImage img(assemble(kContent.size(), 1, kContent, kTimestamp, kSignature));
    applicationImage app(img.path(), test_logger());
    const std::vector<uint8_t> sig = app.getSignature();
    ASSERT_EQ(sig.size(), kSignature.size());
    EXPECT_EQ(std::memcmp(sig.data(), kSignature.data(), sig.size()), 0);
}

TEST(ApplicationImageSignature, MissingSignatureRejected)
{
    TempImage img(assemble(kContent.size(), 1, kContent, kTimestamp, /*signature=*/""));
    applicationImage app(img.path(), test_logger());
    EXPECT_EQ(reject_of([&] { app.getSignature(); }), Reject::open);
}

TEST(ApplicationImageSignature, SignatureEndsBeforeAppendedCertificate)
{
    const std::string pem_trailer = "\n-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n";
    TempImage img(assemble(kContent.size(), 1, kContent, kTimestamp, kSignature,
                           /*corrupt_crc=*/false, pem_trailer));
    applicationImage app(img.path(), test_logger());
    const std::vector<uint8_t> sig = app.getSignature();
    EXPECT_EQ(sig.size(), kSignature.size());
}

TEST(ApplicationImageCopy, CopyRoundTripsAndReports100)
{
    TempImage img(assemble(kContent.size(), 1, kContent, kTimestamp, kSignature));
    applicationImage app(img.path(), test_logger());

    char tmpl[] = "/tmp/appimg-copy-XXXXXX";
    int fd = mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    close(fd);
    std::string dest = tmpl;

    int last_progress = -1;
    app.copyImage(dest, [&last_progress](int p) { last_progress = p; });
    EXPECT_EQ(last_progress, 100);

    std::ifstream copied(dest, std::ios::binary);
    std::string round_trip((std::istreambuf_iterator<char>(copied)),
                           std::istreambuf_iterator<char>());
    EXPECT_EQ(round_trip, kContent);
    unlink(dest.c_str());
}

TEST(ApplicationImageCopy, OversizedDeclaredSizeFailsWrite)
{
    TempImage img(assemble(1U << 20U, 1, kContent, kTimestamp, kSignature));
    applicationImage app(img.path(), test_logger());
    EXPECT_EQ(reject_of([&] { app.copyImage("/tmp/appimg-copy-oversize"); }),
              Reject::write_error);
    unlink("/tmp/appimg-copy-oversize");
}

TEST(ApplicationImageCopy, UnwritableDestinationFailsWrite)
{
    TempImage img(assemble(kContent.size(), 1, kContent, kTimestamp, kSignature));
    applicationImage app(img.path(), test_logger());
    EXPECT_EQ(reject_of([&] { app.copyImage("/nonexistent-dir/appimg-copy"); }),
              Reject::write_error);
}

} // namespace
