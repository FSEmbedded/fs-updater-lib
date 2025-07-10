#include "updateApplication.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"

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

#include <regex>
#include <fstream>
#include <chrono>
#include <ctime>
#include "../subprocess/subprocess.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#ifndef APPLICATION_VERSION_REGEX_STRING
#define APPLICATION_VERSION_REGEX_STRING "[0-9]{8}"
#endif

namespace updater {

// CertificateVerifier Implementation
CertificateVerifier::CertificateVerifier(const std::string& keyring_path,
                                        std::shared_ptr<logger::LoggerHandler> logger)
    : keyring_path_(keyring_path), logger_(logger) {}

bool CertificateVerifier::verify_certificate_chain(const std::vector<Botan::X509_Certificate>& chain) {
    if (chain.empty()) {
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Empty certificate chain provided", logger::logLevel::ERROR));
        return false;
    }

    try {
        std::vector<Botan::X509_Certificate> trusted_certs = load_trusted_certificates();
        if (trusted_certs.empty()) {
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                config::APP_UPDATE, "No trusted certificates found in keyring", logger::logLevel::ERROR));
            return false;
        }

        const Botan::X509_Certificate& leaf = chain.front();
        std::vector<Botan::X509_Certificate> intermediates(chain.begin() + 1, chain.end());

        //log_certificate_info(leaf, "Leaf certificate");
        return validate_certificate_chain(leaf, intermediates, trusted_certs);

    } catch (const std::exception& e) {
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Exception in verify_certificate_chain: " + std::string(e.what()),
            logger::logLevel::ERROR));
        return false;
    }
}

std::vector<Botan::X509_Certificate> CertificateVerifier::extract_certificates_from_image(
    const std::filesystem::path& image_path) {

    constexpr std::string_view PEM_BEGIN = "-----BEGIN CERTIFICATE-----";
    constexpr std::string_view PEM_END = "-----END CERTIFICATE-----";

    std::ifstream in{image_path, std::ios::binary};
    if (!in.is_open()) {
        throw std::runtime_error("Unable to open image file: " + image_path.string());
    }

    uint64_t file_size = std::filesystem::file_size(image_path);
    if (file_size < config::HEADER_SIZE) {
        throw std::runtime_error("File too small to contain valid header");
    }

    // Read and parse header
    std::vector<uint8_t> header_data(config::HEADER_SIZE);
    in.read(reinterpret_cast<char*>(header_data.data()), config::HEADER_SIZE);
    if (in.gcount() != static_cast<std::streamsize>(config::HEADER_SIZE)) {
        throw std::runtime_error("Failed to read complete header");
    }

    HeaderParser::ImageHeader header = HeaderParser::parse(header_data);
    if (!header.is_valid()) {
        throw std::runtime_error("Invalid header data");
    }

    // Seek past SquashFS content to certificate section
    const auto seek_pos = static_cast<std::streamoff>(config::HEADER_SIZE + header.squashfs_size);
    in.seekg(seek_pos, std::ios::beg);
    if (in.fail()) {
        throw std::runtime_error("Failed to seek past squashfs content");
    }

    // Read remaining content and extract certificates
    std::vector<Botan::X509_Certificate> certificates;
    std::string accumulated;
    accumulated.reserve(16384);

    std::vector<char> buffer(config::CHUNK_SIZE);
    while (in.good() && !in.eof()) {
        in.read(buffer.data(), config::CHUNK_SIZE);
        auto read_bytes = in.gcount();
        if (read_bytes > 0) {
            accumulated.append(buffer.data(), static_cast<size_t>(read_bytes));
        }
    }

    // Parse PEM certificates
    size_t pos = 0;
    while (pos < accumulated.size()) {
        auto begin_pos = accumulated.find(PEM_BEGIN, pos);
        if (begin_pos == std::string::npos) break;

        auto end_pos = accumulated.find(PEM_END, begin_pos + PEM_BEGIN.size());
        if (end_pos == std::string::npos) break;

        end_pos += PEM_END.size();
        if (end_pos < accumulated.size() && accumulated[end_pos] == '\n') {
            ++end_pos;
        }

        std::string pem_block = accumulated.substr(begin_pos, end_pos - begin_pos);
        try {
            Botan::DataSource_Memory src(pem_block);
            certificates.emplace_back(src);
            //log_certificate_info(certificates.back(), "Extracted certificate");
        } catch (const std::exception& e) {
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                config::APP_UPDATE, "Failed to parse certificate: " + std::string(e.what()),
                logger::logLevel::WARNING));
        }
        pos = end_pos;
    }

    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        config::APP_UPDATE, "Extracted " + std::to_string(certificates.size()) + " certificates",
        logger::logLevel::DEBUG));

    return certificates;
}

std::vector<Botan::X509_Certificate> CertificateVerifier::load_trusted_certificates() const {
    if (cache_valid_) {
        return trusted_certs_cache_;
    }

    std::vector<Botan::X509_Certificate> trusted_certs;

    try {
        std::ifstream keyring_file(keyring_path_);
        if (!keyring_file) {
            throw std::runtime_error("Failed to open keyring file: " + keyring_path_);
        }

        std::stringstream buffer;
        buffer << keyring_file.rdbuf();
        std::string content = buffer.str();

        // Parse PEM certificates from keyring
        const std::regex cert_regex("-----BEGIN CERTIFICATE-----[\\s\\S]*?-----END CERTIFICATE-----");
        auto begin = std::sregex_iterator(content.begin(), content.end(), cert_regex);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::string pem = it->str();
            try {
                Botan::DataSource_Memory mem(pem);
                Botan::X509_Certificate cert(mem);
                trusted_certs.push_back(cert);
                //log_certificate_info(cert, "Loaded trusted certificate");
            } catch (const std::exception& e) {
                logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                    config::APP_UPDATE, "Failed to parse trusted certificate: " + std::string(e.what()),
                    logger::logLevel::WARNING));
            }
        }
        // Cache the results
        trusted_certs_cache_ = trusted_certs;
        cache_valid_ = true;
    } catch (const std::exception& e) {
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "load_trusted_certificates: " + std::string(e.what()),
            logger::logLevel::ERROR));
        throw;
    }

    return trusted_certs;
}

bool CertificateVerifier::validate_certificate_chain(
    const Botan::X509_Certificate& leaf,
    const std::vector<Botan::X509_Certificate>& intermediates,
    const std::vector<Botan::X509_Certificate>& trusted_certs) const {

    try {
        // Prepare certificate stores
        Botan::Certificate_Store_In_Memory trusted_store;
        for (const auto& cert : trusted_certs) {
            trusted_store.add_certificate(cert);
        }

        Botan::Certificate_Store_In_Memory intermediate_store;
        for (const auto& cert : intermediates) {
            intermediate_store.add_certificate(cert);
        }

        Botan::Path_Validation_Restrictions restrictions(
            false, // no revocation checking
            80,    // minimum key strength
            false,
            std::chrono::seconds(0)
        );

        std::vector<Botan::Certificate_Store*> cert_stores = {&trusted_store, &intermediate_store};

        // Validate certificate path
        auto result = Botan::x509_path_validate(
            leaf, restrictions, cert_stores, "",
            Botan::Usage_Type::UNSPECIFIED,
            std::chrono::system_clock::now()
        );

        if (!result.successful_validation()) {
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                config::APP_UPDATE, "Certificate chain validation failed: " + result.result_string(),
                logger::logLevel::ERROR));
            return false;
        }

        // Verify leaf certificate matches validated chain
        const auto& validated_chain = result.cert_path();
        if (validated_chain.empty()) {
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                config::APP_UPDATE, "No valid certificate path found",
                logger::logLevel::ERROR));
            return false;
        }

        std::string leaf_fp = leaf.fingerprint(crypto::FINGERPRINT_ALGORITHM);
        std::string validated_fp = validated_chain[0]->fingerprint(crypto::FINGERPRINT_ALGORITHM);

        if (leaf_fp != validated_fp) {
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                config::APP_UPDATE, "Leaf certificate fingerprint mismatch",
                logger::logLevel::ERROR));
            return false;
        }

        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Certificate chain validation succeeded",
            logger::logLevel::DEBUG));

        return true;

    } catch (const Botan::Exception& e) {
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Botan exception during validation: " + std::string(e.what()),
            logger::logLevel::ERROR));
        return false;
    }
}

void CertificateVerifier::log_certificate_info(const Botan::X509_Certificate& cert,
                                              const std::string& context) const {
    std::string subject = cert.subject_dn().to_string();
    std::string fingerprint = cert.fingerprint(crypto::FINGERPRINT_ALGORITHM);
    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        config::APP_UPDATE, context + ": Subject=" + subject + ", SHA-256=" + fingerprint,
        logger::logLevel::DEBUG));
}

// ImageVerifier Implementation
ImageVerifier::ImageVerifier(std::shared_ptr<logger::LoggerHandler> logger)
    : logger_(logger) {}

bool ImageVerifier::verify_header(const std::vector<uint8_t>& header_data,
                                 uint64_t& size, uint32_t& version, uint32_t& crc) const {
    if (header_data.size() != config::HEADER_SIZE) {
        return false;
    }

    HeaderParser::ImageHeader header = HeaderParser::parse(header_data);
    if (!header.is_valid()) {
        return false;
    }

    if (!HeaderParser::validate_crc(header, header_data)) {
        return false;
    }

    size = header.squashfs_size;
    version = header.version;
    crc = header.crc;
    return true;
}

bool ImageVerifier::verify_signature(const Botan::X509_Certificate& cert,
                                     applicationImage& application,
                                     uint64_t squashfs_size,
                                     const std::vector<uint8_t>& timestamp,
                                     const std::vector<uint8_t>& signature) const {
    try {
        Botan::AutoSeeded_RNG rng;
        std::unique_ptr<Botan::Public_Key> pub_key = cert.load_subject_public_key();
        if (!pub_key || !pub_key->check_key(rng, false)) {
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                config::APP_UPDATE, "Invalid public key", logger::logLevel::ERROR));
            return false;
        }

        Botan::PK_Verifier verifier(*pub_key, crypto::SIGNATURE_SCHEME, Botan::IEEE_1363);
        
        // Hash the SquashFS content directly (like original implementation)
        auto crypto_wrapper = [&verifier](char *buffer, uint32_t length) {
            verifier.update(reinterpret_cast<const uint8_t *>(buffer), length);
        };
        application.read_img_content_only(crypto_wrapper, squashfs_size);

        // Add timestamp to hash
        verifier.update(timestamp.data(), timestamp.size());

        return verifier.check_signature(signature);
    } catch (const Botan::Exception& e) {
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Signature verification failed: " + std::string(e.what()),
            logger::logLevel::ERROR));
        return false;
    }
}

uint32_t ImageVerifier::compute_crc32(const std::vector<uint8_t>& data) const {
    uint32_t crc = config::CRC32_INITIAL;

    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ config::CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ config::CRC32_INITIAL;
}

uint64_t ImageVerifier::parse_uint64_be(const uint8_t* data) const {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

uint32_t ImageVerifier::parse_uint32_be(const uint8_t* data) const {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

// HeaderParser Implementation
HeaderParser::ImageHeader HeaderParser::parse(const std::vector<uint8_t>& header_data) {
    if (header_data.size() != config::HEADER_SIZE) {
        return {};
    }

    ImageHeader header;
    header.squashfs_size = 0;
    header.version = 0;
    header.crc = 0;

    // Parse big-endian values
    for (int i = 0; i < 8; ++i) {
        header.squashfs_size = (header.squashfs_size << 8) | header_data[i];
    }
    for (int i = 8; i < 12; ++i) {
        header.version = (header.version << 8) | header_data[i];
    }
    for (int i = 12; i < 16; ++i) {
        header.crc = (header.crc << 8) | header_data[i];
    }

    return header;
}

bool HeaderParser::validate_crc(const ImageHeader& header,
                               const std::vector<uint8_t>& header_data) {
    if (header_data.size() != config::HEADER_SIZE) {
        return false;
    }

    // CRC is calculated over first 12 bytes only
    std::vector<uint8_t> crc_data(header_data.begin(), header_data.begin() + 12);
    uint32_t computed_crc = config::CRC32_INITIAL;
    for (uint8_t byte : crc_data) {
        computed_crc ^= byte;
        for (int i = 0; i < 8; i++) {
            if (computed_crc & 1) {
                computed_crc = (computed_crc >> 1) ^ config::CRC32_POLYNOMIAL;
            } else {
                computed_crc >>= 1;
            }
        }
    }
    computed_crc ^= config::CRC32_INITIAL;

    return computed_crc == header.crc;
}

// Main applicationUpdate Implementation
applicationUpdate::applicationUpdate(const std::shared_ptr<UBoot::UBoot>& uboot_ptr,
                                   const std::shared_ptr<logger::LoggerHandler>& logger)
    : updateBase(uboot_ptr, logger),
      application_image_path_(config::STANDARD_APP_IMG_STORE),
      application_temp_path_(config::STANDARD_APP_IMG_TEMP_STORE),
      tmp_app_path_(std::filesystem::path(config::STANDARD_APP_IMG_STORE) / config::TEMP_APP_FILE) {

    logger->setLogEntry(std::make_shared<logger::LogEntry>(
        config::APP_UPDATE, "applicationUpdate: constructor start", logger::logLevel::DEBUG));

    initialize_from_rauc_config();
    setup_paths();
}

void applicationUpdate::initialize_from_rauc_config() {
    if (!std::filesystem::exists(config::RAUC_SYSTEM_PATH)) {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "RAUC config file not found", logger::logLevel::ERROR));
        throw std::runtime_error("RAUC config file not found");
    }

    try {
        boost::property_tree::ptree rauc_config;
        boost::property_tree::ini_parser::read_ini(config::RAUC_SYSTEM_PATH, rauc_config);

        std::string keyring_path = rauc_config.get<std::string>("keyring.path");
        std::string full_keyring_path = "/etc/rauc/" + keyring_path;

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

        Botan::X509_Certificate signer_cert = embedded_certs.front();

        // Step 2: Verify certificate validity at signing time
        std::chrono::system_clock::time_point signing = application.getTimeOfSigning();
        Botan::X509_Time signing_time(signing);

        if (signing_time < signer_cert.not_before() || signing_time > signer_cert.not_after()) {
            throw std::runtime_error("Certificate was invalid at signing time");
        }

        // Step 3: Verify header
        std::vector<uint8_t> header_data = application.getHeader();
        uint64_t squashfs_size;
        uint32_t version, crc;

        if (!image_verifier_->verify_header(header_data, squashfs_size, version, crc)) {
            throw std::runtime_error("Header verification failed");
        }

        // Step 4: Verify content signature
        std::vector<uint8_t> timestamp = application.getTimestamp();
        std::vector<uint8_t> signature = application.getSignature();

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
        char current_app = get_current_application();
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "Current application: " + std::string(1, current_app),
            logger::logLevel::DEBUG));

        // Determine target path
        std::string target_path = application_image_path_;
        target_path += (current_app == 'A') ? "app_b.squashfs" : "app_a.squashfs";

        applicationImage application(path_to_bundle, logger);

        if (!verify_application_bundle(application)) {
            throw std::runtime_error("Application bundle verification failed");
        }

        perform_installation(path_to_bundle);
        update_boot_variable(current_app);

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

void applicationUpdate::perform_installation(const std::string& source_path) {
    // Remove temporary file if it exists
    std::filesystem::remove(tmp_app_path_);

    // Copy to temporary location
    applicationImage application(source_path, logger);
    application.copyImage(tmp_app_path_.string());

    char current_app = get_current_application();
    std::string target_path = application_image_path_;
    target_path += (current_app == 'A') ? "app_b.squashfs" : "app_a.squashfs";

    // Atomic rename to final location
    std::filesystem::rename(tmp_app_path_, target_path);
}

void applicationUpdate::update_boot_variable(char current_app) {
    char new_app = (current_app == 'A') ? 'B' : 'A';
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
        char current_app = get_current_application();
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

#if defined(UPDATE_VERSION_TYPE_STRING)
version_t applicationUpdate::getCurrentVersion() {
    std::string app_version;
    std::ifstream application_version(config::PATH_TO_APPLICATION_VERSION_FILE);

    if (application_version.good()) {
        std::getline(application_version, app_version);
    } else {
        std::string error_msg = "Failed to read version file";
        if (application_version.eof()) error_msg = "End-of-File reached";
        else if (application_version.fail()) error_msg = "Logical error on I/O operation";
        else if (application_version.bad()) error_msg = "Read/writing error on I/O operation";

        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "getCurrentVersion: " + error_msg, logger::logLevel::ERROR));
        throw std::runtime_error(error_msg);
    }

    return app_version;
}
#elif defined(UPDATE_VERSION_TYPE_UINT64)
version_t applicationUpdate::getCurrentVersion() {
    std::regex file_content_regex(APPLICATION_VERSION_REGEX_STRING);
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

    if (std::regex_match(app_version, file_content_regex)) {
        return std::stoul(app_version);
    } else {
        std::string error_msg = "Version format invalid: " + app_version;
        logger->setLogEntry(std::make_shared<logger::LogEntry>(
            config::APP_UPDATE, "getCurrentVersion: " + error_msg, logger::logLevel::ERROR));
        throw std::runtime_error(error_msg);
    }
}
#endif

} // namespace updater
