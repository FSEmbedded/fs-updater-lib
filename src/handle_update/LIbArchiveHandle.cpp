#include "LibArchiveHandle.h"
#include <archive.h>
#include <archive_entry.h>
#include <iostream>

namespace fs {

LibArchiveHandle::LibArchiveHandle() : m_arch(nullptr)
{
    m_arch = archive_read_new();
    if (!m_arch)
        throw fs::LibArchiveException("Failed to create libarchive handle", ENOMEM);

    // Enable support for tar format
    archive_read_support_format_tar(m_arch);

    // Enable only bzip2 filter
    int r = archive_read_support_filter_bzip2(m_arch);
    if (r != ARCHIVE_OK) {
        std::cerr << "Warning: bz2 support failed: "
                  << archive_error_string(m_arch) << std::endl;
    }
}

LibArchiveHandle::~LibArchiveHandle()
{
    if (m_arch) {
        archive_read_free(m_arch);
        m_arch = nullptr;
    }
}

void LibArchiveHandle::open(const std::filesystem::path &filepath, size_t buffer_size)
{
    if (!m_arch)
        throw LibArchiveException("Archive handle not initialized", EINVAL);

    if (archive_read_open_filename(m_arch, filepath.string().c_str(), buffer_size) != ARCHIVE_OK) {
        std::string error_msg = "Open file " + filepath.string() + " fails: " +
                                archive_error_string(m_arch);
        throw LibArchiveException(error_msg, archive_errno(m_arch));
    }
}

void fs::LibArchiveHandle::open_stream(std::istream &input, size_t buffer_size)
{
    if (!m_arch)
        throw LibArchiveException("Archive handle not initialized", EINVAL);

    // Small RAII struct for the buffer + stream pointer
    struct StreamData {
        std::istream* stream;
        std::vector<char> buffer;
        StreamData(std::istream* s, size_t sz) : stream(s), buffer(sz) {}
    };

    // create unique_ptr for exception-safety up to the point we hand it off
    auto data = std::make_unique<StreamData>(&input, buffer_size);

    // non-capturing read callback (OK to be a lambda -> convertible to function pointer)
    auto read_cb = [](archive*, void* client_data, const void** buff) -> la_ssize_t {
        auto* d = static_cast<StreamData*>(client_data);
        if (d->stream->bad()) return ARCHIVE_FATAL;
        d->stream->read(d->buffer.data(), d->buffer.size());
        std::streamsize n = d->stream->gcount();
        if (n <= 0) {
            if (d->stream->bad()) return ARCHIVE_FATAL;
            if (d->stream->eof()) return ARCHIVE_EOF;
            return ARCHIVE_RETRY;
        }
        *buff = d->buffer.data();
        return static_cast<la_ssize_t>(n);
    };

    // non-capturing close callback: libarchive will call this and we free the StreamData
    auto close_cb = [](archive*, void* client_data) -> int {
        delete static_cast<StreamData*>(client_data);
        return ARCHIVE_OK;
    };

    // Transfer ownership to libarchive: release() is required here.
    StreamData* raw_data = data.release();

    int r = archive_read_open(m_arch, raw_data, /*open_cb*/ nullptr, read_cb, close_cb);
    if (r != ARCHIVE_OK) {
        // cleanup if open failed (libarchive won't call close_cb in that case)
        delete raw_data;

        std::string err = "Failed to open archive from stream";
        if (const char* ae = archive_error_string(m_arch); ae && *ae) err += ": " + std::string(ae);
        throw LibArchiveException(err, archive_errno(m_arch));
    }
}

} // namespace fs
