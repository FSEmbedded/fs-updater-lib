#pragma once

#include "fs_exceptions.h"        // fs::GenericException, fs::LibArchiveException
#include "fs_header_types.h"      // fs::fs_header_v0_0, fs::fs_header_v1_0
#include <filesystem>
#include <string>
#include <memory>
#include <archive.h>
#include <archive_entry.h>

#include <json/json.h>

// Forward declarations for libarchive types
struct archive;
struct archive_entry;

namespace logger { class LoggerHandler; } // forward
namespace fs {
    class LibArchiveHandle; // forward

class UpdateStore
{
  private:
    const std::string app_store_name = "update.app";
    const std::string fw_store_name = "update.fw";
    bool fw_available;
    bool app_available;
    std::shared_ptr<logger::LoggerHandler> logger;
    /**
     * Calculate SHA256 checksum of a file.
     * @param filepath Path to the file
     * @param algorithm Hash algorithm to use (e.g., "SHA-256")
     * @return Hexadecimal string of the checksum
     * @throw GenericException if file cannot be opened or hash calculation fails
     */
    std::string CalculateCheckSum(const std::filesystem::path& filepath, const std::string& algorithm);
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

    std::string getApplicationStoreName()
    {
        return app_store_name;
    }

    std::string getFirmwareStoreName()
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

    void ExtractUpdateStore(const std::filesystem::path &path_to_update_image);
    void ReadUpdateConfiguration(const std::string configuration_path);
    bool CheckUpdateSha256Sum(const std::filesystem::path &path_to_update_image);
};
} // namespace fs
