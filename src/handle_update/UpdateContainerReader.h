#pragma once

#include "Descriptor.h"
#include "fs_header_types.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <istream>
#include <string>

namespace fs {

class UpdateStreamSink; // handle_update/UpdateStreamSink.h

/**
 * On-disk format of an `.fs` update container, as discriminated by the
 * F&S header byte at offset 15 (`info.version`) and the `type[16]` field.
 *
 * - V1_0: legacy `[fs_header v1.0] + tar.bz2` payload. Detected for the
 *   sole purpose of returning a clear error.
 * - V2_0: streaming `[fs_header v2.0] + length-prefixed JSON descriptor +
 *   raw concatenated members`. Read by UpdateContainerReader.
 * - Invalid: anything else (bad magic, unknown version byte, v2.0 header
 *   with the wrong type tag).
 */
enum class FormatVersion : uint8_t {
    Invalid = 0,
    V1_0,
    V2_0,
};

inline constexpr uint8_t FS_HEADER_VERSION_V1_0 = 0x10;
inline constexpr uint8_t FS_HEADER_VERSION_V2_0 = 0x20;

/// Maximum descriptor block size accepted by `open_update_container`.
/// Prevents a malformed length prefix from triggering a huge
/// allocation. Real descriptors are a few KB at most; 64 KB is generous.
inline constexpr std::uint32_t MAX_DESCRIPTOR_BYTES = 64u * 1024u;

/**
 * Identify the update-container format from a parsed F&S header.
 *
 * Pure function: no I/O, no allocation. Caller is responsible for having
 * read the 64-byte header from the source stream into `header`.
 */
[[nodiscard]] inline FormatVersion detect_format_version(const fs_header_v1_0& header) noexcept
{
    if (std::memcmp(header.info.magic, "FSLX", 4) != 0) {
        return FormatVersion::Invalid;
    }
    switch (header.info.version) {
        case FS_HEADER_VERSION_V1_0:
            return FormatVersion::V1_0;
        case FS_HEADER_VERSION_V2_0:
            // v2.0 is only valid when the type tag matches; any other
            // type at version 0x20 is reserved for future variants.
            if (std::strncmp(header.type, "FSUPv2", 6) == 0) {
                return FormatVersion::V2_0;
            }
            return FormatVersion::Invalid;
        default:
            return FormatVersion::Invalid;
    }
}

/**
 * Header + descriptor pair returned by `open_update_container`. After the
 * call the source stream's read position is just past the descriptor
 * block; subsequent `extract_member` calls seek to absolute member
 * offsets, so the position is rewindable but not load-bearing.
 */
struct ContainerHead
{
    fs_header_v1_0 header;
    Descriptor descriptor;
};

/**
 * Read and validate the F&S header of a v2.0 container, then read and
 * parse the length-prefixed JSON descriptor that follows. The source
 * stream must be positioned at the start of the container (byte 0).
 *
 * Throws fs::GenericException on:
 *  - `EIO`: short read of header, length prefix, or descriptor bytes
 *  - `EINVAL`: header is not a v2.0 container, descriptor length prefix
 *     exceeds `MAX_DESCRIPTOR_BYTES`, or descriptor JSON fails
 *     validation (rethrown from parse_descriptor)
 */
[[nodiscard]] ContainerHead open_update_container(std::istream& source);

/**
 * Stream one v2.0 member's bytes from the source `.fs` stream window
 * `[member.offset, member.offset + member.size)` to `sink`, hashing
 * inline with SHA-256, and verify the digest matches `member.sha256`.
 *
 * On success: `sink.commit()` is called, function returns.
 * On any failure (truncated source, hash mismatch): `sink.abort()` is
 * called and a fs::GenericException is thrown. Callers do not need to
 * call `sink.abort()` manually after a throw.
 *
 * Error codes carried in the thrown exception:
 *  - `EIO` if the source ends before `member.size` bytes are read
 *  - `EILSEQ` if the computed SHA-256 doesn't match `member.sha256`
 *
 * If `on_chunk` is non-null, it is called after each successful chunk
 * write with the cumulative bytes successfully written for this
 * member. Callers can divide by `member.size` for a 0..1 ratio.
 * A null callback reports nothing during extraction.
 */
void extract_member(std::istream& source, const Member& member, UpdateStreamSink& sink,
                    std::function<void(std::uint64_t bytes_written)> on_chunk = nullptr);

/**
 * Path-owning wrapper around `open_update_container` + `extract_member`.
 *
 * Owns the source `std::ifstream`; the constructor opens the file but
 * does not yet read any bytes. Call `open()` once to read and validate
 * the F&S header and parse the descriptor; subsequent calls to
 * `extract()` stream individual members through the supplied sink.
 *
 * Typical use from the CLI dispatcher:
 *
 * @code
 * UpdateContainerReader reader(path);
 * reader.open();
 * for (const auto& m : reader.descriptor().members) {
 *     auto sink = make_sink_for(m);
 *     reader.extract(m, *sink);
 * }
 * @endcode
 */
class UpdateContainerReader
{
public:
    /**
     * Store `path`. Does NOT open or validate the container — call
     * `open()` for that. Cheap; never throws.
     */
    explicit UpdateContainerReader(std::string path);

    UpdateContainerReader(const UpdateContainerReader&) = delete;
    UpdateContainerReader& operator=(const UpdateContainerReader&) = delete;
    UpdateContainerReader(UpdateContainerReader&&) = delete;
    UpdateContainerReader& operator=(UpdateContainerReader&&) = delete;

    /**
     * Open the file, read+validate the F&S header, read+parse the
     * descriptor. Throws fs::GenericException on any failure — same
     * error codes as `open_update_container`, plus `ENOENT`/`EACCES` if
     * the file itself cannot be opened. Calling `open()` more than
     * once throws (EBUSY).
     */
    void open();

    /**
     * Parsed descriptor, valid only after `open()` has returned.
     * Throws fs::GenericException(ENODATA) before `open()`.
     */
    [[nodiscard]] const Descriptor& descriptor() const;

    /**
     * Stream `member`'s bytes through `sink`. Caller picks the sink
     * type (FileSink, RaucInstallSink, DiscardSink) based on the
     * member's type. Same error contract as `extract_member`: on
     * throw, `sink.abort()` was called first. `on_chunk` (optional)
     * forwards to `extract_member`'s per-chunk progress hook.
     */
    void extract(const Member& member, UpdateStreamSink& sink,
                 std::function<void(std::uint64_t bytes_written)> on_chunk = nullptr);

private:
    std::string path_;
    std::ifstream source_;
    Descriptor descriptor_;
    bool opened_;
};

} // namespace fs
