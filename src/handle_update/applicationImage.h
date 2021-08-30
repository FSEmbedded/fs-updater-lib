/**
 * Create logical representation of application update bundle.
 *
 * Extract application image from update bundle.
 * Offer interface for certificate verification.
 * Header crc32 checksumm will be calculated and proven.
 */

#pragma once

#include <filesystem>
#include <string>

#include <exception>
#include <stdexcept>

#include <vector>
#include <memory>
#include <functional>

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "updateBase.h"
#include "./../BaseException.h"


#define FILE_CHUNK_BUFFER 512
#define SIZE_CERT_APP_DATE_SIGN 26

constexpr char APPLICATION[] = "application image";

///////////////////////////////////////////////////////////////////////////
/// applicationImage' exception definitions
///////////////////////////////////////////////////////////////////////////

class ReadPointOfTime : public fs::BaseFSUpdateException
{
    public:
        /**
         * Can not read formatted time string.
         * @param time_string Wrong formatted time string.
         */
        explicit ReadPointOfTime(const std::string &time_string)
        {
            this->error_msg = std::string("Error reading timestring: \"") + time_string;
            this->error_msg += std::string("\"");
        }
};

class WrongHeaderVersion : public fs::BaseFSUpdateException
{
    public:
        /**
         * Wrong expected header version.
         * @param header_version Header version of application image.
         */
        explicit WrongHeaderVersion(const uint32_t header_version)
        {
            this->error_msg = std::string("Wrong header version: ") + std::to_string(header_version);
            this->error_msg += std::string(" expected version: 1");
        }
};

class OpenApplicationImage : public fs::BaseFSUpdateException
{
    public:
        /**
         * Can not open application update image.
         * @param path Path to application update image.
         * @param error During file interaction.
         */
        OpenApplicationImage(const std::string &path, const std::string &error)
        {
            this->error_msg = std::string("Error: ") + error + std::string("; ");
            this->error_msg += std::string("Path: ") + path;
        }
};

class WrongHeaderChecksum : public fs::BaseFSUpdateException
{
     public:
        /**
         * Header checksum mismatch.
         * @param crc32_calc Calculated checksum through reading header.
         * @param crc32_header Checksum read in header.
         */
        WrongHeaderChecksum(const uint32_t & crc32_calc, const uint32_t & crc32_header)
        {
            std::stringstream msg;
            msg << "header crc32: " << std::hex <<  "\"" << crc32_header << "\" " <<  "calculated crc32: "  <<  "\"" << crc32_calc << "\"";
            this->error_msg = std::string("Error during header calculation: ") + msg.str();
        }
};

class DuringWriteApplicationImage : public fs::BaseFSUpdateException
{
    public:
        /**
         * Error during writing application image to persistent memory.
         * @param msg Error message.
         */
        explicit DuringWriteApplicationImage(const std::string & msg)
        {
            this->error_msg = std::string("Error during write application to persistent memory: ") + msg;
        }
};

///////////////////////////////////////////////////////////////////////////
/// applicationImage' declaration
///////////////////////////////////////////////////////////////////////////

class applicationImage
{
    private:
        std::filesystem::path path;
        const std::shared_ptr<logger::LoggerHandler> logger;
        uint32_t header_version, crc32_check, header_size;
        uint64_t application_image_size;

    public:
        /**
         * Application image mapping.
         * @param path Path to application image update bundle.
         * @param logger Logger object reference.
         * @throw OpenApplicationImage Can not open application update container.
         * @throw WrongHeaderChecksum Wrong header checksum in application update container.
         * @throw WrongHeaderVersion Header version of update container mismatch with compatible one.
         */
        applicationImage(const std::filesystem::path &, const std::shared_ptr<logger::LoggerHandler> &);
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
        void read_img(std::function<void(char *, uint32_t)> );

        /**
         * Extract application image out of update package and save it in persistent memory.
         * @throw OpenApplicationImage
         * @throw DuringWriteApplicationImage
         */
        void copyImage(const std::filesystem::path &);
};