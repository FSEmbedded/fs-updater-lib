#pragma once

#include <fus_updater_lib/config.h>
#include "../uboot_interface/UBoot.h"
#include "updateBase.h"
#include "applicationImage.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"
#include "./../BaseException.h"

#include <string>
#include <memory>
#include <vector>
#include <cstdint>
// botan is NOT included here: the certificate/image verifiers that carry
// Botan::X509_Certificate are forward-declared below and defined in the
// private cert_image_verifier.h (included only by updateApplication.cpp), so
// consumers (fsupdate.h via handleUpdate.h) don't drag botan transitively.

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

    // CertificateVerifier and ImageVerifier are defined in the private
    // cert_image_verifier.h (they carry Botan::X509_Certificate). Only their
    // forward declarations (above) appear in this public header; applicationUpdate
    // holds them via unique_ptr, so the botan type stays out of the API surface.

    // Separate header parsing class
    class HeaderParser {
    public:
        struct ImageHeader {
            uint64_t squashfs_size;
            uint32_t version;
            uint32_t crc;

            [[nodiscard]] bool is_valid() const {
                return squashfs_size > 0 && version > 0;
            }
        };

        static ImageHeader parse(const std::vector<uint8_t>& header_data);
        static bool validate_crc(const ImageHeader& header,
                               const std::vector<uint8_t>& header_data);
    };

    // Main application update class
    class applicationUpdate : public updateBase {
    private:
        // Core components
        std::unique_ptr<CertificateVerifier> cert_verifier_;
        std::unique_ptr<ImageVerifier> image_verifier_;

        // Paths
        std::string application_image_path_;
        std::string application_temp_path_;
        std::string tmp_app_path_;

        // Configuration
        void initialize_from_rauc_config();
        void setup_paths();

    public:
        // Constructor/Destructor
        applicationUpdate(const std::shared_ptr<UBoot::UBoot>& uboot_ptr,
                         const std::shared_ptr<logger::LoggerHandler>& logger);
        ~applicationUpdate() override; // out-of-line: destroys unique_ptr<Verifier> where the type is complete

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
        [[nodiscard]] std::string getTempAppPath() const { return tmp_app_path_; }

    private:
        // Core verification logic
        bool verify_application_bundle(applicationImage& application);

        // Installation helpers
        void perform_installation(const std::string& source_path,
                                  std::function<void(int)> progress_cb = nullptr);
        void update_boot_variable(char current_app);
        [[nodiscard]] char get_current_application() const;
    };

} // namespace updater
