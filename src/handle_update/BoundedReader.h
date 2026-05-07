#pragma once

#include <algorithm>
#include <ios>
#include <istream>

namespace fs {

/**
 * Range-bounded reader over a seekable `std::istream`.
 *
 * Used by the v2.0 streaming reader to consume a single member's payload
 * (a window of `[offset, offset + size)`) from the source `.fs` file
 * without copying or re-seeking on every byte. Multiple readers can be
 * constructed sequentially over the same source — each constructor seeks
 * the source to its own offset.
 *
 * Not a `std::streambuf` subclass; a plain `read()` method is enough for
 * the chunked hash-and-pipe loop in `forEachMember()` and avoids the
 * overhead of `std::iostream` virtual dispatch.
 */
class BoundedReader
{
public:
    BoundedReader(std::istream& source, std::streamoff offset, std::streamsize size)
        : source_(source), remaining_(size > 0 ? size : 0)
    {
        source_.seekg(offset, std::ios::beg);
    }

    BoundedReader(const BoundedReader&) = delete;
    BoundedReader& operator=(const BoundedReader&) = delete;
    BoundedReader(BoundedReader&&) = delete;
    BoundedReader& operator=(BoundedReader&&) = delete;

    /**
     * Read up to `n` bytes into `buf`. Returns the actual number of bytes
     * read; 0 indicates end-of-window or end-of-source.
     */
    [[nodiscard]] std::streamsize read(char* buf, std::streamsize n) noexcept
    {
        if (remaining_ <= 0 || n <= 0) {
            return 0;
        }
        const std::streamsize to_read = std::min(n, remaining_);
        source_.read(buf, to_read);
        const std::streamsize got = source_.gcount();
        remaining_ -= got;
        return got;
    }

    [[nodiscard]] std::streamsize remaining() const noexcept { return remaining_; }
    [[nodiscard]] bool eof() const noexcept { return remaining_ <= 0; }

private:
    std::istream& source_;
    std::streamsize remaining_;
};

} // namespace fs
