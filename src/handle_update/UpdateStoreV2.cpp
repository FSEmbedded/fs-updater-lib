#include "UpdateStoreV2.h"

#include "BoundedReader.h"
#include "Descriptor.h"
#include "Sha256Hasher.h"
#include "UpdateStreamSink.h"
#include "fs_exceptions.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <string>

namespace fs {

V2OpenResult open_v2_container(std::istream& source)
{
    V2OpenResult result;

    // 1. Read the 64-byte F&S header.
    source.read(reinterpret_cast<char*>(&result.header), sizeof(fs_header_v1_0));
    if (source.gcount() != static_cast<std::streamsize>(sizeof(fs_header_v1_0))) {
        throw GenericException(
            "v2.0 container: failed to read 64-byte F&S header", EIO);
    }

    // 2. Validate that this is actually a v2.0 container.
    if (detect_format_version(result.header) != FormatVersion::V2_0) {
        throw GenericException(
            "v2.0 container: header magic/version/type does not identify a v2.0 container",
            EINVAL);
    }

    // 3. Read u32 little-endian descriptor length prefix.
    unsigned char lenbuf[4];
    source.read(reinterpret_cast<char*>(lenbuf), sizeof(lenbuf));
    if (source.gcount() != static_cast<std::streamsize>(sizeof(lenbuf))) {
        throw GenericException(
            "v2.0 container: failed to read descriptor length prefix", EIO);
    }
    const std::uint32_t desc_len = static_cast<std::uint32_t>(lenbuf[0]) |
                                   (static_cast<std::uint32_t>(lenbuf[1]) << 8) |
                                   (static_cast<std::uint32_t>(lenbuf[2]) << 16) |
                                   (static_cast<std::uint32_t>(lenbuf[3]) << 24);
    if (desc_len > MAX_DESCRIPTOR_BYTES) {
        throw GenericException(
            "v2.0 container: descriptor length " + std::to_string(desc_len) +
                " exceeds cap " + std::to_string(MAX_DESCRIPTOR_BYTES),
            EINVAL);
    }

    // 4. Read the descriptor bytes.
    std::string desc_json(desc_len, '\0');
    source.read(desc_json.data(), static_cast<std::streamsize>(desc_len));
    if (source.gcount() != static_cast<std::streamsize>(desc_len)) {
        throw GenericException(
            "v2.0 container: descriptor truncated (read " +
                std::to_string(source.gcount()) + " of " +
                std::to_string(desc_len) + " bytes)",
            EIO);
    }

    // 5. Parse — rethrows GenericException from parse_descriptor.
    result.descriptor = parse_descriptor(desc_json);
    return result;
}

V2ContainerReader::V2ContainerReader(std::filesystem::path path)
    : path_(std::move(path)), source_{}, descriptor_{}, opened_(false)
{
}

void V2ContainerReader::open()
{
    if (opened_) {
        throw GenericException("V2ContainerReader::open() called more than once", EBUSY);
    }
    source_.open(path_, std::ios::binary);
    if (!source_.good()) {
        throw GenericException(
            "v2.0 container: failed to open '" + path_.string() + "' for reading",
            errno != 0 ? errno : ENOENT);
    }
    auto result = open_v2_container(source_);
    descriptor_ = std::move(result.descriptor);
    opened_ = true;
}

const Descriptor& V2ContainerReader::descriptor() const
{
    if (!opened_) {
        throw GenericException("V2ContainerReader::descriptor() called before open()", ENODATA);
    }
    return descriptor_;
}

void V2ContainerReader::extract(const Member& member, UpdateStreamSink& sink)
{
    if (!opened_) {
        throw GenericException("V2ContainerReader::extract() called before open()", ENODATA);
    }
    extract_member(source_, member, sink);
}

namespace {

constexpr std::streamsize kStreamChunk = 8192;

} // namespace

void extract_member(std::istream& source, const Member& member, UpdateStreamSink& sink)
{
    BoundedReader reader(source,
                         static_cast<std::streamoff>(member.offset),
                         static_cast<std::streamsize>(member.size));
    Sha256Hasher hasher;
    char buf[kStreamChunk];

    try {
        while (reader.remaining() > 0) {
            const std::streamsize got = reader.read(buf, kStreamChunk);
            if (got <= 0) {
                throw GenericException(
                    "v2.0 member '" + member.name +
                        "': source truncated before declared size " +
                        std::to_string(member.size),
                    EIO);
            }
            const auto n = static_cast<std::size_t>(got);
            hasher.update(buf, n);
            sink.write(buf, n);
        }
        const std::string actual = hasher.hex_digest();
        if (actual != member.sha256) {
            throw GenericException(
                "v2.0 member '" + member.name +
                    "': sha256 mismatch (expected " + member.sha256 +
                    ", got " + actual + ")",
                EILSEQ);
        }
        sink.commit();
    } catch (...) {
        sink.abort();
        throw;
    }
}

} // namespace fs
