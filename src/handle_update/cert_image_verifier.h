#pragma once
// The botan-carrying verifier definitions live here, kept out of the public
// updateApplication.h so consumers (fsupdate.h via handleUpdate.h) don't
// drag <botan/x509cert.h> transitively. Included ONLY by
// updateApplication.cpp. Mirrors the Sha256Hasher.h firewall pattern
// (forward-decl + unique_ptr in the public header; full definition private).

#include "applicationImage.h"
#include "../logger/LoggerHandler.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <botan/x509cert.h>

namespace updater {

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
            const std::string& image_path);

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
        [[nodiscard]] uint32_t compute_crc32(const std::vector<uint8_t>& data) const;

        // Header parsing utilities
        uint64_t parse_uint64_be(const uint8_t* data) const;
        uint32_t parse_uint32_be(const uint8_t* data) const;
    };

} // namespace updater
