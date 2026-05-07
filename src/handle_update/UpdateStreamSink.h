#pragma once

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <ios>
#include <string>
#include <utility>

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
        if (out_.is_open()) {
            out_.write(data, static_cast<std::streamsize>(n));
        }
    }

    void commit() override
    {
        if (done_) {
            return;
        }
        out_.flush();
        out_.close();
        std::rename(tmp_path_.c_str(), final_path_.c_str());
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
        std::remove(tmp_path_.c_str());
        done_ = true;
    }

private:
    std::string final_path_;
    std::string tmp_path_;
    std::ofstream out_;
    bool done_;
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
