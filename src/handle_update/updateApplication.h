#pragma once

#include <fus_updater_lib/config.h>
#include "../uboot_interface/IUBootEnv.h"
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

// Image-format constants + HeaderParser live in the dependency-free
// app_image_format.h (shared with the verification TU and the native tests).
#include "app_image_format.h"

// Configuration constants
namespace updater::config {
    constexpr char RAUC_SYSTEM_PATH[] = "/etc/rauc/system.conf";
    // Cmake-overridable via -DFSUP_APP_IMG_STORE=... (the BSP recipe sets
    // this to match the RAUC app-bundle install hook's staging directory —
    // meta-fus sets it to FUS_UPDATE_APP_IMG_DIR, /data/app/images). Default
    // is the generic fsup-framework /rw_fs convention (same default pattern
    // as fs::DEFAULT_RAUC_SCRATCH_PATH / FSUP_RAUC_SCRATCH).
    constexpr const char* STANDARD_APP_IMG_STORE = FUS_LIB_APP_IMG_STORE;
    constexpr char STANDARD_APP_IMG_TEMP_STORE[] = "/tmp/application_package";
    constexpr char PATH_TO_APPLICATION_VERSION_FILE[] = "/etc/app_version";
    constexpr char TEMP_APP_FILE[] = "tmp.app";
}

namespace updater {

    // Forward declarations
    class CertificateVerifier;
    class ImageVerifier;

    // CertificateVerifier and ImageVerifier are defined in the private
    // cert_image_verifier.h (they carry Botan::X509_Certificate). Only their
    // forward declarations (above) appear in this public header; applicationUpdate
    // holds them via unique_ptr, so the botan type stays out of the API surface.

    // Main application update class
    class applicationUpdate : public updateBase {
    private:
        // Core components
        std::unique_ptr<CertificateVerifier> cert_verifier_;
        std::unique_ptr<ImageVerifier> image_verifier_;

        // Paths
        std::string rauc_config_path_;
        std::string application_image_path_;
        std::string application_temp_path_;
        std::string tmp_app_path_;

        // Configuration
        void initialize_from_rauc_config();
        void setup_paths();

    public:
        // Constructor/Destructor
        // rauc_config_path/app_image_store_path default to the production
        // constants; overridable so tests can point the constructor's
        // keyring/config load at a fixture directory instead of /etc/rauc.
        applicationUpdate(const std::shared_ptr<UBoot::IUBootEnv>& uboot_ptr,
                         const std::shared_ptr<logger::LoggerHandler>& logger,
                         std::string rauc_config_path = config::RAUC_SYSTEM_PATH,
                         std::string app_image_store_path = config::STANDARD_APP_IMG_STORE);
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

    protected:
        /**
         * Perform the RAUC D-Bus install (InstallBundle + wait for
         * completion). Pure virtual: the real implementation lives in
         * RaucApplicationUpdate (RaucApplicationUpdate.{h,cpp}), kept out of
         * this translation unit so the lightweight native test build (which
         * excludes rauc_dbus_client/UBoot's libubootenv dependency) can still
         * compile+link every other applicationUpdate method against a test
         * double that overrides this one.
         */
        virtual void install_bundle_via_rauc(const std::string& path_to_bundle) = 0;

    private:
        // Core verification logic
        bool verify_application_bundle(applicationImage& application);

        // Installation helpers
        void perform_installation(const std::string& source_path,
                                  std::function<void(int)> progress_cb = nullptr);
        void install_rauc_bundle(const std::string& path_to_bundle, char current_app);
        void update_boot_variable(char current_app);
        [[nodiscard]] char get_current_application() const;
    };

} // namespace updater
