#include "UpdateContainerReader.h"

#include "BoundedReader.h"
#include "Descriptor.h"
#include "Sha256Hasher.h"
#include "UpdateStreamSink.h"
#include "fs_exceptions.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fs {

namespace {

std::uint64_t container_end_from(const fs_header_v1_0& header)
{
    // info.file_size_* declares the byte count after the 64-byte header.
    const std::uint64_t after_header =
        static_cast<std::uint64_t>(header.info.file_size_low) |
        (static_cast<std::uint64_t>(header.info.file_size_high) << 32);
    return sizeof(fs_header_v1_0) + after_header;
}

void validate_member_layout(const Descriptor& descriptor,
                            std::uint64_t payload_start,
                            std::uint64_t container_end)
{
    std::set<std::string_view> names;
    bool have_firmware = false;
    bool have_application = false;
    std::vector<const Member*> windows;

    for (const auto& member : descriptor.members) {
        if (!names.insert(member.name).second) {
            throw GenericException(
                "v2.0 container: duplicate member name '" + member.name + "'",
                EINVAL);
        }
        if (member.type == MemberType::Firmware) {
            if (have_firmware) {
                throw GenericException(
                    "v2.0 container: more than one firmware member", EINVAL);
            }
            have_firmware = true;
        }
        if (member.type == MemberType::Application) {
            if (have_application) {
                throw GenericException(
                    "v2.0 container: more than one application member", EINVAL);
            }
            have_application = true;
        }

        // An empty window reads nothing; only non-empty windows must
        // lie inside the payload region (overflow-safe formulation).
        if (member.size == 0) {
            continue;
        }
        if (member.offset < payload_start || member.offset > container_end ||
            member.size > container_end - member.offset) {
            throw GenericException(
                "v2.0 container: member '" + member.name + "' window [" +
                    std::to_string(member.offset) + ", " +
                    std::to_string(member.offset) + "+" +
                    std::to_string(member.size) +
                    ") outside payload region [" +
                    std::to_string(payload_start) + ", " +
                    std::to_string(container_end) + ")",
                EINVAL);
        }
        windows.push_back(&member);
    }

    std::sort(windows.begin(), windows.end(),
              [](const Member* a, const Member* b) { return a->offset < b->offset; });
    for (std::size_t i = 0; i + 1 < windows.size(); ++i) {
        if (windows[i]->offset + windows[i]->size > windows[i + 1]->offset) {
            throw GenericException(
                "v2.0 container: member '" + windows[i]->name +
                    "' overlaps member '" + windows[i + 1]->name + "'",
                EINVAL);
        }
    }
}

} // namespace

ContainerHead open_update_container(std::istream& source)
{
    ContainerHead result;

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

    // 6. Layout invariants: every non-empty member window must lie inside
    //    the payload region the header declares, without duplicates or
    //    overlap — reject deterministically here instead of failing late
    //    in extraction with a misleading EIO/EILSEQ.
    const std::uint64_t payload_start =
        sizeof(fs_header_v1_0) + sizeof(lenbuf) + desc_len;
    validate_member_layout(result.descriptor, payload_start,
                           container_end_from(result.header));
    return result;
}

UpdateContainerReader::UpdateContainerReader(std::string path)
    : path_(std::move(path)), source_{}, descriptor_{}, opened_(false)
{
}

void UpdateContainerReader::open()
{
    if (opened_) {
        throw GenericException("UpdateContainerReader::open() called more than once", EBUSY);
    }
    source_.open(path_, std::ios::binary);
    if (!source_.good()) {
        throw GenericException(
            "v2.0 container: failed to open '" + path_ + "' for reading",
            errno != 0 ? errno : ENOENT);
    }
    auto result = open_update_container(source_);
    descriptor_ = std::move(result.descriptor);
    opened_ = true;
}

const Descriptor& UpdateContainerReader::descriptor() const
{
    if (!opened_) {
        throw GenericException("UpdateContainerReader::descriptor() called before open()", ENODATA);
    }
    return descriptor_;
}

void UpdateContainerReader::extract(const Member& member, UpdateStreamSink& sink,
                                std::function<void(std::uint64_t)> on_chunk)
{
    if (!opened_) {
        throw GenericException("UpdateContainerReader::extract() called before open()", ENODATA);
    }
    extract_member(source_, member, sink, std::move(on_chunk));
}

namespace {

constexpr std::streamsize kStreamChunk = 8192;

} // namespace

void extract_member(std::istream& source, const Member& member, UpdateStreamSink& sink,
                    std::function<void(std::uint64_t)> on_chunk)
{
    BoundedReader reader(source,
                         static_cast<std::streamoff>(member.offset),
                         static_cast<std::streamsize>(member.size));
    Sha256Hasher hasher;
    char buf[kStreamChunk];
    std::uint64_t bytes_done = 0;

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
            bytes_done += n;
            if (on_chunk) { on_chunk(bytes_done);
}
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
