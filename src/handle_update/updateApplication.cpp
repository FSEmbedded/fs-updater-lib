#include "updateApplication.h"
#include "app_bundle_install.h"
#include "cert_image_verifier.h" // private: CertificateVerifier/ImageVerifier defs (carry botan)
#include "../uboot_interface/allowed_uboot_variable_states.h"
#include "util/posix_utils.h"

#include <botan/pkix_types.h>
#include <botan/x509path.h>
#include <botan/certstor.h>
#include <botan/auto_rng.h>
#include <botan/hash.h>
#include <botan/hex.h>
#include <botan/x509cert.h>
#include <botan/pubkey.h>
#include <botan/pk_keys.h>
#include <botan/rng.h>
#include <botan/data_src.h>

#include <algorithm>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cerrno>
#include <stdexcept>

extern "C" {
    #include <fcntl.h>
    #include <unistd.h>
}

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

namespace updater {

// Main applicationUpdate Implementation
applicationUpdate::applicationUpdate(const std::shared_ptr<UBoot::IUBootEnv>& uboot_ptr,
                                   const std::shared_ptr<logger::LoggerHandler>& logger,
                                   std::string rauc_config_path,
                                   std::string app_image_store_path)
    : updateBase(uboot_ptr, logger),
      rauc_config_path_(std::move(rauc_config_path)),
      application_image_path_(app_image_store_path),
      application_temp_path_(config::STANDARD_APP_IMG_TEMP_STORE),
      tmp_app_path_(fs::util::path_join(app_image_store_path, config::TEMP_APP_FILE)) {

    logger->setLogEntry(std::make_shared<logger::LogEntry>(
        config::APP_UPDATE, "applicationUpdate: constructor start", logger::logLevel::DEBUG));

    initialize_from_rauc_config();
    setup_paths();
}

// Out-of-line dtor: destroys the unique_ptr<CertificateVerifier>/<ImageVerifier>
// members here, where both verifier types are complete (via cert_image_verifier.h).
// Defining it in the header would force botan back into the public interface.
applicationUpdate::~applicationUpdate() = default;

void applicationUpdate::initialize_from_rauc_config() {
    if (!fs::util::path_exists(rauc_config_path_)) {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "RAUC config file not found", logger::logLevel::ERROR));
        throw std::runtime_error("RAUC config file not found");
    }

    try {
        boost::property_tree::ptree rauc_config;
        boost::property_tree::ini_parser::read_ini(rauc_config_path_, rauc_config);

        std::string keyring_path = rauc_config.get<std::string>("keyring.path");
        const auto slash_pos = rauc_config_path_.find_last_of('/');
        std::string const rauc_config_dir =
            (slash_pos == std::string::npos) ? "." : rauc_config_path_.substr(0, slash_pos);
        std::string const full_keyring_path = (!keyring_path.empty() && keyring_path[0] == '/')
            ? keyring_path
            : rauc_config_dir + "/" + keyring_path;

        // Initialize certificate verifier
        cert_verifier_ = std::make_unique<CertificateVerifier>(full_keyring_path, logger);

        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "RAUC config loaded successfully", logger::logLevel::DEBUG));
    } catch (const std::exception& e) {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "RAUC config error: " + std::string(e.what()), logger::logLevel::ERROR));
        throw;
    }
}

void applicationUpdate::setup_paths() {
    // Initialize image verifier
    image_verifier_ = std::make_unique<ImageVerifier>(logger);

    logger->setLogEntry(std::make_shared<logger::LogEntry>(
        config::APP_UPDATE, "Paths and verifiers initialized", logger::logLevel::DEBUG));
}

bool applicationUpdate::verify_application_bundle(applicationImage& application) {
    try {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Starting application bundle verification", logger::logLevel::DEBUG));

        // Step 1: Extract and verify certificates
        std::vector<Botan::X509_Certificate> embedded_certs =
            cert_verifier_->extract_certificates_from_image(application.getPath());

        if (embedded_certs.empty()) {
            throw std::runtime_error("No certificates found in application image");
        }

        if (!cert_verifier_->verify_certificate_chain(embedded_certs)) {
            throw std::runtime_error("Certificate chain verification failed");
        }

        Botan::X509_Certificate const signer_cert = embedded_certs.front();

        // Step 2: Verify certificate validity at signing time
        std::chrono::system_clock::time_point const signing = application.getTimeOfSigning();
        Botan::X509_Time const signing_time(signing);

        if (signing_time < signer_cert.not_before() || signing_time > signer_cert.not_after()) {
            throw std::runtime_error("Certificate was invalid at signing time");
        }

        // Step 3: Verify header
        std::vector<uint8_t> const header_data = application.getHeader();
        uint64_t squashfs_size;
        uint32_t version, crc;

        if (!image_verifier_->verify_header(header_data, squashfs_size, version, crc)) {
            throw std::runtime_error("Header verification failed");
        }

        // Step 4: Verify content signature
        std::vector<uint8_t> const timestamp = application.getTimestamp();
        std::vector<uint8_t> const signature = application.getSignature();

        if (!image_verifier_->verify_signature(signer_cert, application, squashfs_size, timestamp, signature)) {
            throw std::runtime_error("Signature verification failed");
        }

        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Application bundle verification successful", logger::logLevel::DEBUG));

        return true;

    } catch (const std::exception& e) {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Bundle verification failed: " + std::string(e.what()),
            logger::logLevel::ERROR));
        return false;
    }
}

void applicationUpdate::install(const std::string& path_to_bundle) {
    try {
        char const current_app = get_current_application();
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Current application: " + std::string(1, current_app),
            logger::logLevel::DEBUG));

        if (fs::is_rauc_bundle_payload(path_to_bundle)) {
            /* Trust chain on this path: RAUC verifies the bundle signature
             * against the system keyring; there is no F&S image header or
             * embedded certificate layer to check. */
            install_rauc_bundle(path_to_bundle, current_app);
        } else {
            applicationImage application(path_to_bundle, logger);

            if (!verify_application_bundle(application)) {
                throw std::runtime_error("Application bundle verification failed");
            }

            perform_installation(path_to_bundle, progress_cb_);
        }

        {
            /* Bracketed so a failed write cannot leave the new slot pointer
             * staged: the caller's error handler stages its failure state on
             * the same environment object and flushes, and anything left
             * behind would ride along and flip a slot this install never
             * completed. Closing the scope drops what was not written.
             */
            UBoot::EnvTransaction const txn(*uboot_handler);
            update_boot_variable(current_app);
            uboot_handler->flushEnvironment();
        }

        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Application installation completed successfully",
            logger::logLevel::DEBUG));

    } catch (const std::exception& e) {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Installation failed: " + std::string(e.what()),
            logger::logLevel::ERROR));
        throw;
    }
}

void applicationUpdate::perform_installation(const std::string& source_path,
                                              std::function<void(int)> progress_cb) {
    // Remove temporary file if it exists (best-effort; a real error fails the install).
    if (!fs::util::remove_file(tmp_app_path_) && errno != ENOENT) {
        throw std::runtime_error("Unable to remove temporary file: " + tmp_app_path_);
    }

    // Copy to temporary location
    applicationImage application(source_path, logger);
    application.copyImage(tmp_app_path_, progress_cb);

    char const current_app = get_current_application();
    const std::string target_path =
        fs::app_slot_image_path(application_image_path_, (current_app == 'A') ? 'B' : 'A');

    // Atomic rename to final location
    if (!fs::util::rename_file(tmp_app_path_, target_path)) {
        throw std::runtime_error("Unable to rename " + tmp_app_path_ + " to " + target_path);
    }

    // fsync directory
    int const dir_fd = open(application_image_path_.c_str(), O_DIRECTORY | O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }
}

void applicationUpdate::install_rauc_bundle(const std::string& path_to_bundle,
                                            char current_app) {
    /* Only installBundle + waitForCompletion are mirrored from the firmware
     * engine: BOOT_ORDER handling is firmware-A/B-only; application A/B is
     * the `application` variable flipped by install()'s shared tail. The
     * bundle's slot install hook stages the image at the incoming path. */
    install_bundle_via_rauc(path_to_bundle);

    std::string images_dir = application_image_path_;
    if (!images_dir.empty() && images_dir.back() == '/') {
        images_dir.pop_back();
    }

    const std::string target = fs::activate_incoming_app_image(images_dir, current_app);

    logger->setLogEntry(std::make_shared<logger::LogEntry>(
        config::APP_UPDATE, "install: activated application image " + target,
        logger::logLevel::DEBUG));
}

void applicationUpdate::update_boot_variable(char current_app) {
    char const new_app = (current_app == 'A') ? 'B' : 'A';
    uboot_handler->addVariable("application", std::string(1, new_app));
}

char applicationUpdate::get_current_application() const {
    try {
        return uboot_handler->getVariable("application", allowed_application_variables);
    } catch (const std::exception& err) {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Could not get UBoot variable: " + std::string(err.what()),
            logger::logLevel::ERROR));
        throw;
    }
}

void applicationUpdate::rollback() {
    try {
        char const current_app = get_current_application();
        update_boot_variable(current_app);

        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Application rollback completed", logger::logLevel::DEBUG));
    } catch (const std::exception& e) {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Rollback failed: " + std::string(e.what()),
            logger::logLevel::ERROR));
        throw;
    }
}

#if UPDATE_VERSION_TYPE_STRING == 1
version_t applicationUpdate::getCurrentVersion() {
    std::string app_version;
    std::ifstream application_version(config::PATH_TO_APPLICATION_VERSION_FILE);

    if (application_version.good()) {
        std::getline(application_version, app_version);
    } else {
        std::string error_msg = "Failed to read version file";
        if (application_version.eof()) { error_msg = "End-of-File reached";
        } else if (application_version.fail()) { error_msg = "Logical error on I/O operation";
        } else if (application_version.bad()) { error_msg = "Read/writing error on I/O operation";
}

        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "getCurrentVersion: " + error_msg, logger::logLevel::ERROR));
        throw std::runtime_error(error_msg);
    }

    return app_version;
}
#elif UPDATE_VERSION_TYPE_UINT64 == 1

static bool is_8digit_version(const std::string &s)
{
    return s.size() == 8 && std::all_of(s.begin(), s.end(),
        [](unsigned char c){ return c >= '0' && c <= '9'; });
}

version_t applicationUpdate::getCurrentVersion() {
    std::string app_version;

    std::ifstream application_version(config::PATH_TO_APPLICATION_VERSION_FILE);
    if (application_version.good()) {
        std::getline(application_version, app_version);
    } else {
        std::string error_msg = "Failed to read version file";
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "getCurrentVersion: " + error_msg, logger::logLevel::ERROR));
        throw std::runtime_error(error_msg);
    }

    if (is_8digit_version(app_version)) {
        return std::stoul(app_version);
    } else {
        std::string error_msg = "Version format invalid: " + app_version;
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "getCurrentVersion: " + error_msg, logger::logLevel::ERROR));
        throw std::runtime_error(error_msg);
    }
}
#else
#error "No valid version type defined"
#endif

} // namespace updater
