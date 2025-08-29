#pragma once

#include "fs_exceptions.h"        // fs::GenericException, fs::LibArchiveException
#include <filesystem>
#include <string>
#include <memory>
#include <archive.h>
#include <archive_entry.h>

#include <json/json.h>

/* namespace block */
using namespace std;

// Forward declarations für libarchive types
struct archive;
struct archive_entry;

namespace logger { class LoggerHandler; } // forward
namespace fs {
    class LibArchiveHandle; // forward

/* fsheader structures */
struct fs_header_v0_0
{                            /* Size: 16 Bytes */
    char magic[4];           /* "FS" + two bytes operating system */
                             /* (e.g. "LX" for Linux) */
    uint32_t file_size_low;  /* Image size [31:0] */
    uint32_t file_size_high; /* Image size [63:32] */
    uint16_t flags;          /* See flags below */
    uint8_t padsize;         /* Number of padded bytes at end */
    uint8_t version;         /* Header version x.y:
                                 [7:4] major x, [3:0] minor y */
};

struct fs_header_v1_0
{                               /* Size: 64 bytes */
    struct fs_header_v0_0 info; /* Image info, see above */
    char type[16];              /* Image type, e.g. "U-BOOT" */
    union {
        char descr[32];   /* Description, null-terminated */
        uint8_t p8[32];   /* 8-bit parameters */
        uint16_t p16[16]; /* 16-bit parameters */
        uint32_t p32[8];  /* 32-bit parameters */
        uint64_t p64[4];  /* 64-bit parameters */
    } param;
};

class UpdateStore
{
  private:
    const string app_store_name = "update.app";
    const string fw_store_name = "update.fw";
    bool fw_available;
    bool app_available;
    shared_ptr<logger::LoggerHandler> logger;
    /**
     * Calculate SHA256 checksum of a file.
     * @param filepath Path to the file
     * @param algorithm Hash algorithm to use (e.g., "SHA-256")
     * @return Hexadecimal string of the checksum
     * @throw GenericException if file cannot be opened or hash calculation fails
     */
    string CalculateCheckSum(const filesystem::path& filepath, const string& algorithm);
    void ExtractTarBz2Internal(archive* a, const std::filesystem::path& targetdir);
  protected:
    Json::Value root;

    bool parseFSUpdateJsonConfig();
    /**
     * Extract tar.bz2 archive to target directory.
     * @param filepath Path to the tar.bz2 file
     * @param targetdir Target directory to extract files into
     * @throw GenericException if extraction fails
     */
    void ExtractTarBz2(const std::filesystem::path& filepath, const std::filesystem::path& targetdir);
    /* * Extract tar.bz2 archive to target directory.
     * @param archive_handle LibArchiveHandle object for managing libarchive resources
     * @param targetdir Target directory to extract files into
     * @throw GenericException if extraction fails
     */
    void ExtractTarBz2(LibArchiveHandle &archive_handle, const std::filesystem::path& targetdir);

  public:
    bool IsFirmwareAvailable()
    {
        return fw_available;
    }
    void SetFirmwareAvailable(bool available)
    {
        this->fw_available = available;
    }
    bool IsApplicationAvailable()
    {
        return app_available;
    }
    void SetApplicationAvailable(bool available)
    {
        this->app_available = available;
    }

    string getApplicationStoreName()
    {
        return app_store_name;
    }

    string getFirmwareStoreName()
    {
        return fw_store_name;
    }

  public:
    UpdateStore();
    ~UpdateStore() = default;

    UpdateStore(const UpdateStore &) = delete;
    UpdateStore &operator=(const UpdateStore &) = delete;
    UpdateStore(UpdateStore &&) = delete;
    UpdateStore &operator=(UpdateStore &&) = delete;

    void ExtractUpdateStore(const filesystem::path &path_to_update_image);
    void ReadUpdateConfiguration(const string configuration_path);
    bool CheckUpdateSha256Sum(const filesystem::path &path_to_update_image);
};
} // namespace fs
