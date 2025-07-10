// Header file improvements (updateApplication.h)

#pragma once

#include <fus_updater_lib/config.h>
#include "../uboot_interface/UBoot.h"
#include "updateBase.h"
#include "applicationImage.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"
#include "./../BaseException.h"

#include <filesystem>
#include <memory>
#include <vector>
#include <cstdint>
#include <botan/x509cert.h>

// Configuration constants
namespace updater::config {
    constexpr char RAUC_SYSTEM_PATH[] = "/etc/rauc/system.conf";
    constexpr char STANDARD_APP_IMG_STORE[] = "/rw_fs/root/application/";
    constexpr char STANDARD_APP_IMG_TEMP_STORE[] = "/tmp/application_package";
    constexpr char PATH_TO_APPLICATION_VERSION_FILE[] = "/etc/app_version";
    constexpr char TEMP_APP_FILE[] = "tmp.app";
    constexpr char APP_UPDATE[] = "application update";

    // Image format constants
    constexpr std::size_t HEADER_SIZE = 16;
    constexpr std::size_t CHUNK_SIZE = 4096;
    constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;
    constexpr uint32_t CRC32_INITIAL = 0xFFFFFFFF;
}

namespace updater {

    // Forward declarations
    class CertificateVerifier;
    class ImageVerifier;
    class HeaderParser;

    // Separate certificate verification class
    class CertificateVerifier {
    private:
        std::string keyring_path_;
        std::shared_ptr<logger::LoggerHandler> logger_;

        // Cache for loaded certificates
        mutable std::vector<Botan::X509_Certificate> trusted_certs_cache_;
        mutable bool cache_valid_ = false;

    public:
        explicit CertificateVerifier(const std::string& keyring_path,
                                   std::shared_ptr<logger::LoggerHandler> logger);

        // Main verification methods
        bool verify_certificate_chain(const std::vector<Botan::X509_Certificate>& chain);
        std::vector<Botan::X509_Certificate> extract_certificates_from_image(
            const std::filesystem::path& image_path);

    private:
        // Certificate loading and validation
        std::vector<Botan::X509_Certificate> load_trusted_certificates() const;
        bool validate_certificate_chain(
            const Botan::X509_Certificate& leaf,
            const std::vector<Botan::X509_Certificate>& intermediates,
            const std::vector<Botan::X509_Certificate>& trusted_certs) const;

        // Utility methods
        void log_certificate_info(const Botan::X509_Certificate& cert,
                                 const std::string& context) const;
        void clear_cache() const { cache_valid_ = false; trusted_certs_cache_.clear(); }
    };

    // Separate image verification class
    class ImageVerifier {
    private:
        std::shared_ptr<logger::LoggerHandler> logger_;

    public:
        explicit ImageVerifier(std::shared_ptr<logger::LoggerHandler> logger);

        // Header verification
        bool verify_header(const std::vector<uint8_t>& header_data, uint64_t& size,
                          uint32_t& version, uint32_t& crc) const;

        // Signature verification
        bool verify_signature(const Botan::X509_Certificate& cert,
                            applicationImage& application,
                            uint64_t squashfs_size,
                            const std::vector<uint8_t>& timestamp,
                            const std::vector<uint8_t>& signature) const;

    private:
        // CRC calculation
        uint32_t compute_crc32(const std::vector<uint8_t>& data) const;

        // Header parsing utilities
        uint64_t parse_uint64_be(const uint8_t* data) const;
        uint32_t parse_uint32_be(const uint8_t* data) const;
    };

    // Separate header parsing class
    class HeaderParser {
    public:
        struct ImageHeader {
            uint64_t squashfs_size;
            uint32_t version;
            uint32_t crc;

            bool is_valid() const {
                return squashfs_size > 0 && version > 0;
            }
        };

        static ImageHeader parse(const std::vector<uint8_t>& header_data);
        static bool validate_crc(const ImageHeader& header,
                               const std::vector<uint8_t>& header_data);
    };

    // Main application update class (simplified)
    class applicationUpdate : public updateBase {
    private:
        // Core components
        std::unique_ptr<CertificateVerifier> cert_verifier_;
        std::unique_ptr<ImageVerifier> image_verifier_;

        // Paths
        std::string application_image_path_;
        std::string application_temp_path_;
        std::filesystem::path tmp_app_path_;

        // Configuration
        void initialize_from_rauc_config();
        void setup_paths();

    public:
        // Constructor/Destructor
        applicationUpdate(const std::shared_ptr<UBoot::UBoot>& uboot_ptr,
                         const std::shared_ptr<logger::LoggerHandler>& logger);
        ~applicationUpdate() override = default;

        // Disable copy/move
        applicationUpdate(const applicationUpdate&) = delete;
        applicationUpdate& operator=(const applicationUpdate&) = delete;
        applicationUpdate(applicationUpdate&&) = delete;
        applicationUpdate& operator=(applicationUpdate&&) = delete;

        // Public interface
        void install(const std::string& path_to_bundle) override;
        void rollback() override;
        version_t getCurrentVersion() override;

        // Utility methods
        std::filesystem::path getTempAppPath() const { return tmp_app_path_; }

    private:
        // Core verification logic
        bool verify_application_bundle(applicationImage& application);

        // Installation helpers
        void perform_installation(const std::string& source_path);
        void update_boot_variable(char current_app);
        char get_current_application() const;
    };

} // namespace updater
