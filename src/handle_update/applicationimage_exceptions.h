#pragma once
// Lightweight applicationImage exception definitions (global namespace), split out of
// applicationImage.h so consumers (and the classification test) can use them without
// pulling the engine headers.
#include "../BaseException.h"
#include <string>
#include <sstream>

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

class ImageUpdatePackageToSmall : public fs::BaseFSUpdateException
{
    public:
        /**
         * The application image update package is too small
         */
        ImageUpdatePackageToSmall()
        {
            this->error_msg = "The update package is too small; Header cannot be parsed and/or no body is given";
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
