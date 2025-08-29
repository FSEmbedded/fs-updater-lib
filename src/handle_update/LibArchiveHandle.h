#pragma once

#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <istream>
#include <memory>
#include <vector>
#include <string>
#include "fs_exceptions.h"
#include "fs_consts.h"

namespace fs {

/**
 * RAII wrapper for libarchive archive* handles.
 * Automatically initializes and frees the libarchive handle.
 * Supports tar + bzip2 compressed archives only.
 * TODO: Extend to support other formats/compressions if needed.
 */
class LibArchiveHandle {
private:
    archive* m_arch;  // underlying libarchive handle

    // Internal RAII structure for stream reading
    struct StreamDataRAII {
        std::istream* stream;         // pointer to input stream
        std::vector<char> buffer;     // buffer for libarchive reading

        StreamDataRAII(std::istream& s, size_t buf_size)
            : stream(&s), buffer(buf_size) {}

        StreamDataRAII(const StreamDataRAII&) = delete;
        StreamDataRAII& operator=(const StreamDataRAII&) = delete;
        StreamDataRAII(StreamDataRAII&&) = default;
        StreamDataRAII& operator=(StreamDataRAII&&) = default;
    };

public:

    // Constructor: initialize archive for reading tar + bz2
    LibArchiveHandle();

    // Destructor: free archive handle automatically
    ~LibArchiveHandle();

    // Access underlying archive pointer
    archive* get() const { return m_arch; }

    /**
     * Open archive from a file
     * @param filepath Path to the archive file
     * @param buffer_size Buffer size for reading (default: BUFFER_SIZE)
     * @throw GenericException if opening fails
     */
    void open(const std::filesystem::path& filepath, size_t buffer_size = BUFFER_SIZE);

    /**
     * Open archive from an input stream
     * @param input Input stream
     * @param buffer_size Buffer size for reading (default: STREAM_BUFFER_SIZE)
     * @throw GenericException if opening fails
     */
    void open_stream(std::istream &input, size_t buffer_size = STREAM_BUFFER_SIZE);

    // Non-copyable, non-movable
    LibArchiveHandle(const LibArchiveHandle&) = delete;
    LibArchiveHandle& operator=(const LibArchiveHandle&) = delete;
    LibArchiveHandle(LibArchiveHandle&&) = delete;
    LibArchiveHandle& operator=(LibArchiveHandle&&) = delete;
};

} // namespace fs
