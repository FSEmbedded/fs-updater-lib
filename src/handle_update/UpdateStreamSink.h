#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <utility>

#include "fs_exceptions.h"
#include "util/posix_utils.h" // fs::util::rename_file / remove_file (no <cstdio>)

namespace fs {

/**
 * Destination for the bytes of one v2.0 update member.
 *
 * The streaming reader's per-member loop calls `write()` repeatedly with
 * fixed-size chunks. When the member's declared size has been delivered
 * and its SHA-256 has matched the descriptor, the reader calls
 * `commit()`. On any failure the reader calls `abort()` and the sink
 * must leave its destination in the same state as before construction.
 *
 * Implementations: `FileSink` (atomic write to a final path),
 * `DiscardSink` (verify-only, no destination).
 */
class UpdateStreamSink
{
public:
    UpdateStreamSink() = default;
    virtual ~UpdateStreamSink() = default;
    UpdateStreamSink(const UpdateStreamSink&) = delete;
    UpdateStreamSink& operator=(const UpdateStreamSink&) = delete;
    UpdateStreamSink(UpdateStreamSink&&) = delete;
    UpdateStreamSink& operator=(UpdateStreamSink&&) = delete;

    virtual void write(const char* data, std::size_t n) = 0;
    virtual void commit() = 0;
    virtual void abort() = 0;
};

/**
 * Atomic write to a final filesystem path.
 *
 * Bytes go to `<path>.tmp` while streaming; `commit()` renames to
 * `<path>`. `abort()` (and the destructor on an uncommitted sink)
 * removes the tmp file, so the final path is never observed in a
 * partial state.
 */
class FileSink : public UpdateStreamSink
{
public:
    explicit FileSink(std::string final_path)
        : final_path_(std::move(final_path)),
          tmp_path_(final_path_ + ".tmp"),
          out_(tmp_path_, std::ios::binary | std::ios::trunc),
          done_(false)
    {
    }

    ~FileSink() override
    {
        if (!done_) {
            abort();
        }
    }

    void write(const char* data, std::size_t n) override
    {
        if (!out_.is_open()) {
            throw GenericException(
                "FileSink::write: stream is not open for " + tmp_path_, EIO);
        }
        out_.write(data, static_cast<std::streamsize>(n));
        if (!out_) {
            /* ofstream signals failure via failbit/badbit without
             * throwing by default; surface it explicitly so the v2
             * extract loop's SHA-then-commit doesn't certify a
             * truncated file as good. */
            throw GenericException(
                "FileSink::write: ofstream error after writing " +
                    std::to_string(n) + " bytes to " + tmp_path_,
                EIO);
        }
        bytes_written_ += n;
    }

    void commit() override
    {
        if (done_) {
            return;
        }
        out_.flush();
        if (!out_) {
            throw GenericException(
                "FileSink::commit: flush failed for " + tmp_path_, EIO);
        }
        out_.close();
        if (out_.is_open() || out_.fail()) {
            throw GenericException(
                "FileSink::commit: close failed for " + tmp_path_, EIO);
        }
        if (!fs::util::rename_file(tmp_path_, final_path_)) {
            const int saved = errno;
            throw GenericException(
                "FileSink::commit: rename " + tmp_path_ + " -> " +
                    final_path_ + " failed",
                saved);
        }
        done_ = true;
    }

    void abort() override
    {
        if (done_) {
            return;
        }
        if (out_.is_open()) {
            out_.close();
        }
        (void)fs::util::remove_file(tmp_path_);
        done_ = true;
    }

    /** Bytes successfully accepted by the stream (post-failbit-check).
     *  Callers can cross-check against the descriptor's declared size
     *  before committing the rename. */
    std::uint64_t bytes_written() const noexcept { return bytes_written_; }

private:
    std::string final_path_;
    std::string tmp_path_;
    std::ofstream out_;
    bool done_;
    std::uint64_t bytes_written_ = 0;
};

/** Verify-only sink: accepts and discards. Used for `--verify` mode. */
class DiscardSink : public UpdateStreamSink
{
public:
    void write(const char* /*data*/, std::size_t /*n*/) override {}
    void commit() override {}
    void abort() override {}
};

} // namespace fs
