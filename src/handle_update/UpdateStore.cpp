#include "UpdateStore.h"
#include "LibArchiveHandle.h"
#include "../BaseException.h"
// include logger definitions because we call logger->setLogEntry() etc.
#include "../uboot_interface/UBoot.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"
#include "handleUpdate.h"
#include "fs_consts.h"
#include <archive.h>
#include <archive_entry.h>
#include <botan/hash.h>
#include <botan/hex.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <vector>
#include <system_error>


namespace fs {

static constexpr unsigned char to_lower(unsigned char c)
{
    return tolower(static_cast<unsigned char>(c));
}

UpdateStore::UpdateStore()
{
    this->app_available = false;
    this->fw_available = false;
}

void UpdateStore::ReadUpdateConfiguration(const string configuration_path)
{
    Json::CharReaderBuilder builder;
    string errs;
    /* create stream for update configuration file */
    ifstream update_configuration(configuration_path, ifstream::in);
    /* check if the stream good is and no error flags are set */
    if (!update_configuration.good())
    {
        if (update_configuration.bad() || update_configuration.fail())
        {
            throw GenericException(configuration_path, ENOENT);
        }
    }
    /* parse */
    if (!Json::parseFromStream(builder, update_configuration, &root, &errs))
    {
        update_configuration.close();
        string fails("Parsing of update configuration fails.");
        throw GenericException(fails, ENOENT);
    }
    update_configuration.close();
}

bool UpdateStore::CheckUpdateSha256Sum(const filesystem::path &path_to_update_image)
{
    if (root.isMember("images") && !root["images"].empty())
    {
        Json::Value images = root["images"];
        if (images.isMember("updates") && !images["updates"].empty())
        {
            Json::Value updates = images["updates"];
            string sha256_str;
            string update_image_file;

            for (Json::Value::iterator counter = updates.begin(); counter != updates.end(); ++counter)
            {
                if(!(*counter).isMember("version") || !(*counter).isMember("handler")
                || !(*counter).isMember("file") || !(*counter).isMember("hashes"))
                {
                    /* wrong format nodes needed */
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, "Nodes version, handler, files or hashes not available.",
                    logger::logLevel::DEBUG));
                    errno = ENOENT;
                    return false;
                }

                update_image_file = (*counter)["file"].asString();
                sha256_str = (*counter)["hashes"]["sha256"].asString();
                /* transform to low for hash compare */
                transform(sha256_str.begin(), sha256_str.end(), sha256_str.begin(), to_lower);
                filesystem::path image_full_path(path_to_update_image / update_image_file);

                string calc_hash = CalculateCheckSum(image_full_path, string("SHA-256"));
                if (calc_hash != sha256_str)
                {
                    cerr << "Hash compare of " << image_full_path << " fails." << endl;
                    return false;
                }

                if (update_image_file.compare(app_store_name) == 0)
                {
                    this->SetApplicationAvailable(true);
                }
                else if (update_image_file.compare(fw_store_name) == 0)
                {
                    this->SetFirmwareAvailable(true);
                }
                else
                {
                    cerr << "Image " << update_image_file << " is not supported." << endl;
                    errno = EINVAL;
                    return false;
                }
            }
        }
        else
        {
            /* wrong json file format. node updates needed..*/
            const string fails = "Node updates is not available or empty.";
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN, fails, logger::logLevel::DEBUG));
            errno = EINVAL;
            return false;
        }
    }
    else
    {
        /* wrong json file format. node images needed..*/
        const string fails = "Node images not available or empty.";
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, fails, logger::logLevel::DEBUG));
        errno = EINVAL;
        return false;
    }

    return true;
}

void UpdateStore::ExtractUpdateStore(const filesystem::path &path_to_update_image)
{
    unique_ptr<struct fs_header_v1_0> fsheader10 = make_unique<struct fs_header_v1_0>();
    ifstream update_img(path_to_update_image, (ifstream::in | ifstream::binary));
    string update_image_file = path_to_update_image;
    // open file
    if (!update_img.good())
    {
        if (update_img.bad() || update_img.fail())
        {
            string error_str = string("Open file ") + update_image_file + string("fails");
            throw GenericException(update_image_file, -EACCES);
        }
    }

    // Read header and validate full header read
    update_img.read(reinterpret_cast<char *>(fsheader10.get()), static_cast<std::streamsize>(sizeof(struct fs_header_v1_0)));
    if (update_img.gcount() != static_cast<std::streamsize>(sizeof(struct fs_header_v1_0)))
    {
        throw GenericException("Failed to read full header from " + update_image_file, -EIO);
    }

    uint64_t file_size = 0;
    file_size = fsheader10->info.file_size_high & 0xFFFFFFFF;
    file_size = file_size << 32;
    file_size = file_size | (fsheader10->info.file_size_low & 0xFFFFFFFF);

    if (strncmp("CERT", fsheader10->type, 4) && (file_size > 0))
    {
        throw GenericException(string("Update has wrong format"), -ENOENT);
    }

    // Validate compressed archive size matches remaining file size
    auto current_pos = update_img.tellg();
    update_img.seekg(0, std::ios::end);
    auto file_end = update_img.tellg();
    auto actual_archive_size = file_end - current_pos;

    if (actual_archive_size < 0)
    {
        throw GenericException("File shorter than expected after header", -EIO);
    }

    if (static_cast<uint64_t>(actual_archive_size) != file_size)
    {
        std::string error_msg = "Archive size mismatch - expected: " +
                                std::to_string(file_size) +
                                ", actual: " + std::to_string(actual_archive_size);
        throw GenericException(error_msg, -EINVAL);
    }

    /* set stream position to the start of the archive */
    update_img.seekg(current_pos);
    if (!update_img.good())
    {
        throw GenericException("seekg() to archive start failed", -EIO);
    }

    try
    {
        fs::LibArchiveHandle archive_handle;
        archive_handle.open_stream(update_img);

        // Extract archive - no size validation here since it's the uncompressed size
        ExtractTarBz2(archive_handle, TARGET_ARCHIV_DIR_PATH);

        // Optional: Validate extracted content size if needed
        // ValidateExtractedContent(TARGET_ARCHIV_DIR_PATH);
    }
    catch (const std::exception &ex)
    {
        throw GenericException("Failed to extract archive: " + std::string(ex.what()), errno);
    }
}

string UpdateStore::CalculateCheckSum(const filesystem::path &filepath, const string &algorithm)
{
    try
    {
        ifstream file(filepath, ios::binary);
        if (!file)
        {
            throw GenericException("Open file " + filepath.string() + " fails.", errno);
        }
        auto hash = Botan::HashFunction::create(algorithm);
        vector<char> buffer(BUFFER_SIZE);
        const uint8_t *ubytes = reinterpret_cast<const uint8_t *>(buffer.data());
        char *buf_data = buffer.data();

        while (file) {
            file.read(buffer.data(), static_cast<streamsize>(BUFFER_SIZE));
            streamsize got = file.gcount();
            if (got > 0) {
                hash->update(ubytes, static_cast<std::size_t>(got));
            }
        }

        vector<uint8_t> output(hash->output_length());
        hash->final(output.data());
        string hashstr = Botan::hex_encode(output);
        /* transform to low for compare */
        transform(hashstr.begin(), hashstr.end(), hashstr.begin(), to_lower);
        return hashstr;
    }
    catch (const exception &ex)
    {
        throw GenericException(string(ex.what()), errno);
    }
}

void UpdateStore::ExtractTarBz2(const filesystem::path &filepath, const filesystem::path &targetdir)
{
    LibArchiveHandle handle;
    handle.open(filepath);
    ExtractTarBz2Internal(handle.get(), targetdir);
}

void UpdateStore::ExtractTarBz2(LibArchiveHandle &archive_handle, const filesystem::path &targetdir)
{
    ExtractTarBz2Internal(archive_handle.get(), targetdir);
}

void UpdateStore::ExtractTarBz2Internal(struct archive* a, const std::filesystem::path& targetdir)
{
#ifdef TEST_EXTRACT_TIME
    auto start = std::chrono::high_resolution_clock::now();
#endif

    if (!a) throw GenericException("archive handle null", -EINVAL);

    /* RAII wrapper for archive_write_disk
     * Ensures automatic cleanup when function exits, even on expttions
     */
    struct WriteArchiveDestructor {
        void operator()(archive* arch) const noexcept{
            if (arch) archive_write_free(arch);
        }
    };
    using ArchivePtr = std::unique_ptr<archive, WriteArchiveDestructor>;

    /* Normalize target directory to avoid repeated conversions */
    std::filesystem::path canonical_target;
    try {
        canonical_target = std::filesystem::weakly_canonical(targetdir);
    } catch (...) {
        canonical_target = std::filesystem::absolute(targetdir);
    }

    const std::string canonical_target_str = canonical_target.generic_string() + '/';

    /* Create RAII-managed disk writer handle */
    ArchivePtr disk_archive(archive_write_disk_new());
    if (!disk_archive) throw GenericException("Failed to create archive_write_disk handle", ENOMEM);

    /* Set extraction options (permissions, ownership, sparse files, etc.) */
    archive_write_disk_set_options(disk_archive.get(),
        ARCHIVE_EXTRACT_TIME |
        ARCHIVE_EXTRACT_PERM |
        ARCHIVE_EXTRACT_ACL |
        ARCHIVE_EXTRACT_FFLAGS |
        ARCHIVE_EXTRACT_OWNER |
        ARCHIVE_EXTRACT_SPARSE);
    archive_write_disk_set_standard_lookup(disk_archive.get());

    ::archive_entry* entry = nullptr;
    uint64_t total_extracted_size = 0;
    size_t file_count = 0;

    while (true) {
        int r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) {
            std::string err = archive_error_string(a) ? archive_error_string(a) : "Unknown libarchive error";
            throw GenericException("archive_read_next_header failed: " + err, archive_errno(a));
        }

        const char* entry_pathname = archive_entry_pathname(entry);
        if (!entry_pathname) {
            throw GenericException("Invalid entry pathname in archive", -EINVAL);
        }

        /* Prevent directory traversal attacks by normalizing paths */
        std::filesystem::path rel = std::filesystem::path(entry_pathname).lexically_normal();

        /* Convert absolute paths to relative */
        if (rel.is_absolute()) {
            rel = rel.lexically_relative("/");
            if (rel.empty() || rel == ".") {
                /* skip problematic entries */
                continue;
            }
        }

        /* Safe construction of destination full path */
        const std::filesystem::path dest_full = (canonical_target / rel).lexically_normal();
        const std::string dest_full_str = dest_full.generic_string();

        /* Security check: ensure extracted path is inside target dir */
        if (dest_full_str.size() < canonical_target_str.size() ||
            dest_full_str.compare(0, canonical_target_str.size(), canonical_target_str) != 0) {
            throw GenericException(std::string("Archive contains unsafe path: ") + entry_pathname, -EPERM);
        }

        /* Set pathname for libarchive extraction */
        archive_entry_set_pathname(entry, dest_full_str.c_str());

        /* Write header (creates directory/file) */
        r = archive_write_header(disk_archive.get(), entry);
        if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
            std::string err = archive_error_string(disk_archive.get()) ? archive_error_string(disk_archive.get()) : "Unknown write_disk error";
            throw GenericException("archive_write_header failed for entry: " + std::string(entry_pathname) + " - " + err, archive_errno(disk_archive.get()));
        }

        /* Extract data blocks */
        const void* buff;
        size_t size;
        la_int64_t offset;
        bool extraction_successful = true;

        while (true) {
            r = archive_read_data_block(a, &buff, &size, &offset);

            if (r == ARCHIVE_EOF) {
                /* finished reading this entry */
                break;
            } else if (r == ARCHIVE_OK) {
                int wr = archive_write_data_block(disk_archive.get(), buff, size, offset);
                if (wr != ARCHIVE_OK && wr != ARCHIVE_WARN) {
                    std::string err = archive_error_string(disk_archive.get()) ? archive_error_string(disk_archive.get()) : "Unknown write_disk error";
                    throw GenericException("archive_write_data_block failed for entry: " + std::string(entry_pathname) + " - " + err, archive_errno(disk_archive.get()));
                }
                /* accumulate total size */
                total_extracted_size += size;
            } else if (r == ARCHIVE_WARN) {
                /* Log warning but continue extraction */
                std::string warn = archive_error_string(a) ? archive_error_string(a) : "Unknown libarchive warning";
                if (this->logger) {
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(FSUPDATE_DOMAIN,
                        "Archive warning for " + std::string(entry_pathname) + ": " + warn, logger::logLevel::WARNING));
                }
                continue;
            } else {
                std::string err = archive_error_string(a) ? archive_error_string(a) : "Unknown libarchive error";
                throw GenericException("archive_read_data_block error for entry: " + std::string(entry_pathname) + " - " + err, archive_errno(a));
            }
        }

        /* Finish entry */
        r = archive_write_finish_entry(disk_archive.get());
        if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
            std::string err = archive_error_string(disk_archive.get()) ? archive_error_string(disk_archive.get()) : "Unknown write_disk finish error";
            throw GenericException("archive_write_finish_entry failed for entry: " + std::string(entry_pathname) + " - " + err, archive_errno(disk_archive.get()));
        }
        /*  count successfully extracted file */
        ++file_count;
    }

    if (file_count == 0) {
        throw GenericException("No files extracted from archive", -ENODATA);
    }

#ifdef TEST_EXTRACT_TIME
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Extract time: " << elapsed.count() << " secs. ";
    std::cout << "Files: " << file_count << ", Total size: " << total_extracted_size << " bytes" << std::endl;
#endif
}
} // namespace fs
