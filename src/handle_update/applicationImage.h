/**
 * Create logical representation of application update bundle.
 *
 * Extract application image from update bundle.
 * Offer interface for certificate verification.
 * Header crc32 checksumm will be calculated and proven.
 */

#pragma once

#include <string>
#include <fstream>
#include <sstream>

#include <exception>
#include <stdexcept>

#include <vector>
#include <memory>
#include <functional>

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include "applicationimage_exceptions.h"

inline constexpr size_t SIZE_CERT_APP_DATE_SIGN = 26;

namespace crypto {
    // Hash algorithm configuration
    inline const std::string HASH_ALGORITHM = "SHA-256";
    constexpr size_t HASH_SIZE = 32;  // SHA-256 output size

    // Signature algorithm configuration
    inline const std::string SIGNATURE_SCHEME = "PSSR(SHA-256)";

    // Certificate fingerprint algorithm
    inline const std::string FINGERPRINT_ALGORITHM = "SHA-256";
}

constexpr char APPLICATION[] = "application image";

///////////////////////////////////////////////////////////////////////////
/// applicationImage' declaration
///////////////////////////////////////////////////////////////////////////

class applicationImage
{
    private:
        std::string path;
        const std::shared_ptr<logger::LoggerHandler> logger;
        uint32_t header_version, crc32_check, header_size;
        uint64_t application_image_size;
        std::ifstream application;

      public:
        /**
         * Application image mapping.
         * @param path Path to application image update bundle.
         * @param logger Logger object reference.
         * @throw OpenApplicationImage Can not open application update container.
         * @throw WrongHeaderChecksum Wrong header checksum in application update container.
         * @throw WrongHeaderVersion Header version of update container mismatch with compatible one.
         */
        applicationImage(const std::string & /*path*/, const std::shared_ptr<logger::LoggerHandler> & /*logger*/);
        ~applicationImage();

        applicationImage(const applicationImage &) = delete;
        applicationImage &operator=(const applicationImage &) = delete;
        applicationImage(applicationImage &&) = delete;
        applicationImage &operator=(applicationImage &&) = delete;

        /**
         * Get size of application image.
         * @return Size of application image
         */
        uint64_t getSizeOfImage() const;

        /**
         * Get time of signing application image.
         * @return Time object.
         */
        std::chrono::system_clock::time_point getTimeOfSigning();

        /**
         * Get signature of application image and time string.
         * @return array of unsigned byte array.
         */
        std::vector<uint8_t> getSignature();

        /**
         * Get Path of application update container.
         * @return Path to application update container as string.
         */
        std::string getPath() const;

        /**
         * Read application image in chunks.
         * Feed callback function which provide specific interface.
         * @param Callback function(array-pointer, length of privided array).
         * @throw OpenApplicationImage
         */
        void read_img(std::function<void(char *, uint32_t)>  /*func*/);

        /**
         * Extract application image out of update package and save it in persistent memory.
         * If progress_cb is set it is called with completion percentage (0-100) after each
         * chunk written; 100 is guaranteed to be called on success before this returns.
         * @throw OpenApplicationImage
         * @throw DuringWriteApplicationImage
         */
        void copyImage(const std::string& dest,
                       std::function<void(int)> progress_cb = nullptr);
        /**
         * Get header data (size + version + CRC).
         * @return Header data as byte vector.
         */
        std::vector<uint8_t> getHeader();

        /**
         * Get timestamp from signature block.
         * @return Timestamp data as byte vector.
         */
        std::vector<uint8_t> getTimestamp();

        /**
         * Read only image content (without header, signature, certificates).
         * @param func Callback function for processing chunks.
         * @param content_size Size of content to read.
         * @throw OpenApplicationImage
         */
        void read_img_content_only(std::function<void(char *, uint32_t)> func, uint64_t content_size);
};
