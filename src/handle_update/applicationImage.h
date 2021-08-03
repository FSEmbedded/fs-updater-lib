#pragma once

#include <filesystem>
#include <string>
#include <fstream>
#include <exception>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <vector>
#include <limits>
#include <memory>
#include <iterator>
#include <sstream>
#include <functional>

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "updateBase.h"
#include "./../BaseException.h"


extern "C" {
    #include "zlib.h"
}

#define FILE_CHUNK_BUFFER 512
#define SIZE_CERT_APP_DATE_SIGN 26

constexpr char APPLICATION[] = "application image";

///////////////////////////////////////////////////////////////////////////
/// applicationImage' exception definitions
///////////////////////////////////////////////////////////////////////////

class NotApplicationImage : public fs::BaseFSUpdateException
{
    public:
        explicit NotApplicationImage(const std::filesystem::path & path)
        {
            this->error_msg = std::string("file is no application: ") + path.string();
        }
};

class ErrorReadPointOfTime : public fs::BaseFSUpdateException
{
    public:
        explicit ErrorReadPointOfTime(const std::string &time_string)
        {
            this->error_msg = std::string("Error reading timestring: \"") + time_string;
            this->error_msg += std::string("\"");
        }
};

class WrongHeaderVersion : public fs::BaseFSUpdateException
{
    public:
        explicit WrongHeaderVersion(const uint32_t header_version)
        {
            this->error_msg = std::string("Wrong header version: ") + std::to_string(header_version);
            this->error_msg += std::string(" exüected version: 1");
        }
};

class ErrorOpenApplicationImage : public fs::BaseFSUpdateException
{
    public:
        ErrorOpenApplicationImage(const std::string &path, const std::string &error)
        {
            this->error_msg = std::string("Error: ") + error + std::string("; ");
            this->error_msg += std::string("Path: ") + path;
        }
};

class WrongHeaderChecksum : public fs::BaseFSUpdateException
{
     public:
        WrongHeaderChecksum(const uint32_t & crc32_calc, const uint32_t & crc32_header)
        {
            std::stringstream msg;
            msg << "header crc32: " << std::hex <<  "\"" << crc32_header << "\" " <<  "calculated crc32: "  <<  "\"" << crc32_calc << "\"";
            this->error_msg = std::string("Error during header calculation: ") + msg.str();
        }
};

class ErrorDuringWriteApplicationImage : public fs::BaseFSUpdateException
{
    public:
        explicit ErrorDuringWriteApplicationImage(const std::string & msg)
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
        applicationImage(const std::filesystem::path &, const std::shared_ptr<logger::LoggerHandler> &);
        ~applicationImage();

        applicationImage(const applicationImage &) = delete;
        applicationImage &operator=(const applicationImage &) = delete;
        applicationImage(applicationImage &&) = delete;
        applicationImage &operator=(applicationImage &&) = delete;

        uint64_t getSizeOfImage() const;
        std::chrono::system_clock::time_point getTimeOfSignign();
        std::vector<uint8_t> getSignature();
        std::string getPath() const;

        void read_img(std::function<void(char *, uint32_t)> );

        void copyImage(const std::filesystem::path &);
};