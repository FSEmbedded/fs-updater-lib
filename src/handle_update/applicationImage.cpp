#include "applicationImage.h"
#include "utils.h"

extern "C" {
    #include <zlib.h>
    #include <fcntl.h>
    #include <unistd.h>
}

#include <iterator>
#include <sstream>
#include <chrono>
#include <ctime>
#include <limits>
#include <iostream>
#include <cstring>

// Botan for certificate handling
#include <botan/x509cert.h>
#include <botan/data_src.h>
#include <botan/pubkey.h>

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

applicationImage::applicationImage(const std::string & path, const std::shared_ptr<logger::LoggerHandler> & logger):
    path(path),
    logger(logger),
    header_size(4+8+4)
{
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("constructor: application image path: ") + path, logger::logLevel::DEBUG));
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("File does not exist: " + path);
    }

    this->application = std::ifstream(this->path, std::ifstream::binary);

    if (!this->application.is_open())
    {
        const std::string error_msg = "Could not open application image file: " + path;
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("constructor: ") + error_msg, logger::logLevel::ERROR));
        throw(OpenApplicationImage(path, error_msg));
    }

    if (!application.good())
    {
        const std::string error_msg = util::describe_stream_error(application);
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("constructor: ") + error_msg, logger::logLevel::ERROR));
        throw(OpenApplicationImage(path, error_msg));
    }

    // Verify mandatory file size
    uint64_t image_size = std::filesystem::file_size(path);
    if (image_size <= header_size)
    {
        throw(ImageUpdatePackageToSmall());
    }

    // read content-header
    unsigned char application_image_size_binary[8] = {0};
    unsigned char header_version_binary[4] = {0};
    unsigned char crc32_checksum_binary[4] = {0};

    application_image_size = 0;
    header_version = 0;

    application.seekg(0, application.beg);

    application.read((char *) application_image_size_binary, 8);
    application.read((char *) header_version_binary, 4);
    application.read((char *) crc32_checksum_binary, 4);


    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        application_image_size = uint64_t(application_image_size_binary[7]);
        application_image_size |= uint64_t(application_image_size_binary[6]) << 8;
        application_image_size |= uint64_t(application_image_size_binary[5]) << 16;
        application_image_size |= uint64_t(application_image_size_binary[4]) << 24;
        application_image_size |= uint64_t(application_image_size_binary[3]) << 32;
        application_image_size |= uint64_t(application_image_size_binary[2]) << 40;
        application_image_size |= uint64_t(application_image_size_binary[1]) << 48;
        application_image_size |= uint64_t(application_image_size_binary[0]) << 56;

        header_version = uint32_t(header_version_binary[3]);
        header_version |= uint32_t(header_version_binary[2]) << 8;
        header_version |= uint32_t(header_version_binary[1]) << 16;
        header_version |= uint32_t(header_version_binary[0]) << 24;

        crc32_check = uint32_t(crc32_checksum_binary[3]);
        crc32_check |= uint32_t(crc32_checksum_binary[2]) << 8;
        crc32_check |= uint32_t(crc32_checksum_binary[1]) << 16;
        crc32_check |= uint32_t(crc32_checksum_binary[0]) << 24;

    #else
        #error "Only defined for little endian. not for big endian systems"
    #endif

    uint32_t crc32_calc = crc32(0L, Z_NULL, 0);

    crc32_calc = crc32(crc32_calc, application_image_size_binary, 8);
    crc32_calc = crc32(crc32_calc, header_version_binary, 4);

    if (header_version != 1)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("constructor: header version: ") + std::to_string(header_version), logger::logLevel::ERROR));
        throw(WrongHeaderVersion(header_version));
    }

    if(crc32_calc != crc32_check)
    {   
        std::stringstream msg;
        msg <<  "header crc32: " << std::hex <<  "\"" << crc32_check << "\" " <<  "calculated crc32: "  <<  "\"" << crc32_calc << "\"";
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("constructor: wrong crc32 checksum: ") + msg.str(), logger::logLevel::ERROR));
        
        throw(WrongHeaderChecksum(crc32_calc, crc32_check));
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("constructor: application image size: ") + std::to_string(application_image_size), logger::logLevel::DEBUG));
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("constructor: header version: ") + std::to_string(header_version), logger::logLevel::DEBUG));
}

applicationImage::~applicationImage()
{
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("application Image deconstructed"), logger::logLevel::DEBUG));
}

uint64_t applicationImage::getSizeOfImage() const
{
    return this->application_image_size;
}

// Read and parse signing timestamp from fixed-size field with dynamic trimming
std::chrono::system_clock::time_point applicationImage::getTimeOfSigning()
{
    application.seekg(application_image_size + header_size, std::ios::beg);

    constexpr size_t MAX_TS = SIZE_CERT_APP_DATE_SIGN;
    char buf[MAX_TS];
    application.read(buf, MAX_TS);
    if (!application.good()) {
        if (application.eof()) {
            const std::string msg = "End-of-File reached on timestamp read";
            logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, "getTimeOfSigning: " + msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, msg));
        }
        const std::string msg = "I/O error reading timestamp";
        logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, "getTimeOfSigning: " + msg, logger::logLevel::ERROR));
        throw(OpenApplicationImage(path, msg));
    }

    // Trim valid ISO timestamp characters
    size_t len = 0;
    while (len < MAX_TS) {
        char c = buf[len];
        bool valid = (c >= '0' && c <= '9') || c=='-' || c==':' || c=='T' || c=='Z' || c=='+';
        if (!valid) break;
        ++len;
    }
    std::string timestr(buf, len);
    // Remove trailing 'Z' if present
    if (!timestr.empty() && timestr.back()=='Z') timestr.pop_back();

    struct tm tm{};
    if (strptime(timestr.c_str(), "%Y-%m-%dT%H:%M:%S", &tm) == nullptr) {
        logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, "getTimeOfSigning: cannot parse timestamp: " + timestr, logger::logLevel::ERROR));
        throw(ReadPointOfTime(timestr));
    }

    logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, "getTimeOfSigning: timestring: " + timestr, logger::logLevel::DEBUG));
    return std::chrono::system_clock::from_time_t(mktime(&tm));
}

std::vector<uint8_t> applicationImage::getSignature()
{
    const uint64_t signature_offset = this->header_size + this->application_image_size + SIZE_CERT_APP_DATE_SIGN;

    application.clear();
    application.seekg(0, std::ios::end);
    if (application.fail()) {
        throw OpenApplicationImage(path, "getSignature: failed to seek to end of file");
    }

    const uint64_t file_size = application.tellg();
    if (file_size <= signature_offset) {
        throw OpenApplicationImage(path, "getSignature: file too small, no signature present");
    }

    // Read the signature+certificate block
    const uint64_t block_size = file_size - signature_offset;
    application.clear();
    application.seekg(signature_offset, std::ios::beg);

    std::vector<char> block(block_size);
    application.read(block.data(), block_size);

    // Find first certificate marker (if any)
    std::string block_str(block.begin(), block.end());
    size_t cert_start = block_str.find("\n-----BEGIN CERTIFICATE-----");

    size_t signature_size;
    if (cert_start != std::string::npos) {
        // Certificates found - signature ends before first certificate
        signature_size = cert_start;
    } else {
        // No certificates - entire block is signature
        signature_size = block_size;
    }

    logger->setLogEntry(std::make_shared<logger::LogEntry>(
        APPLICATION, "getSignature: signature size = " + std::to_string(signature_size), logger::logLevel::DEBUG));

    if (signature_size == 0) {
        throw OpenApplicationImage(path, "getSignature: no signature found");
    }

    // Return only the signature part
    std::vector<uint8_t> signature(block.begin(), block.begin() + signature_size);
    return signature;
}

std::string applicationImage::getPath() const
{
    return this->path;
}

void applicationImage::read_img(std::function<void(char *, uint32_t)> func)
{
    application.seekg(this->header_size, application.beg);
    char BUFFER[FILE_CHUNK_BUFFER] = {0};

    while (application.tellg() < std::streampos(uint64_t(this->header_size) + SIZE_CERT_APP_DATE_SIGN + this->application_image_size - uint64_t(FILE_CHUNK_BUFFER)))
    {
        application.read(BUFFER, FILE_CHUNK_BUFFER);
        if (!application.good())
        {
            const std::string error_msg = util::describe_stream_error(application);
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("read_img: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        func(BUFFER, FILE_CHUNK_BUFFER);
    }
    uint32_t residual_data = this->header_size + this->application_image_size + SIZE_CERT_APP_DATE_SIGN - application.tellg();
    application.read(BUFFER, std::streampos(residual_data));
    if (!application.good())
    {
        const std::string error_msg = util::describe_stream_error(application);
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("read_img: ") + error_msg, logger::logLevel::ERROR));
        throw(OpenApplicationImage(path, error_msg));
    }
    func(BUFFER, residual_data);
}


void applicationImage::copyImage(const std::string &dest)
{
    int fd = -1;
    try
    {
        // open temp file
        fd = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            throw DuringWriteApplicationImage("open() failed: " + std::string(strerror(errno)));
        }

        application.seekg(this->header_size, application.beg);

        uint64_t cursor = 0;
        char buffer[FILE_CHUNK_BUFFER];

        while ((cursor + FILE_CHUNK_BUFFER) <= this->application_image_size)
        {
            application.read(buffer, FILE_CHUNK_BUFFER);

            if (!application.good())
            {
                throw DuringWriteApplicationImage("read error");
            }

            ssize_t written = write(fd, buffer, FILE_CHUNK_BUFFER);
            if (written != FILE_CHUNK_BUFFER)
            {
                throw DuringWriteApplicationImage("write error");
            }

            cursor += FILE_CHUNK_BUFFER;
        }

        // remaining bytes
        size_t remaining = this->application_image_size - cursor;
        if (remaining > 0)
        {
            application.read(buffer, remaining);

            if (!application.good())
            {
                throw DuringWriteApplicationImage("read error (remaining)");
            }

            ssize_t written = write(fd, buffer, remaining);
            if (written != (ssize_t)remaining)
            {
                throw DuringWriteApplicationImage("write error (remaining)");
            }
        }

        // ensure file content is on storage
        if (fsync(fd) != 0)
            throw DuringWriteApplicationImage("fsync(file) failed: " + std::string(strerror(errno)));

        if (close(fd) != 0)
            throw DuringWriteApplicationImage("close(file) failed");
    }
    catch (const std::exception &e)
    {
        if (fd >= 0)
            close(fd);

        unlink(dest.c_str()); // cleanup temp file

        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(
                APPLICATION,
                std::string("copyImage: ") + e.what(),
                logger::logLevel::ERROR));

        throw;
    }
}

std::vector<uint8_t> applicationImage::getHeader()
{
    std::vector<uint8_t> header_data(16); // 8 bytes size + 4 bytes version + 4 bytes CRC

    application.clear();  // reset any error/eof flag
    application.seekg(0, application.beg);
    application.read(reinterpret_cast<char*>(header_data.data()), 16);

    if (!application.good())
    {
        throw OpenApplicationImage(path, "Failed to read header data");
    }

    return header_data;
}

std::vector<uint8_t> applicationImage::getTimestamp()
{
    // Reset any error flags and seek to correct position
    application.clear();
    application.seekg(this->application_image_size + header_size, application.beg);

    if (application.fail()) {
        throw OpenApplicationImage(path, "getTimestamp: failed to seek to timestamp position");
    }

    std::vector<uint8_t> timestamp(SIZE_CERT_APP_DATE_SIGN);
    application.read(reinterpret_cast<char*>(timestamp.data()), SIZE_CERT_APP_DATE_SIGN);

    if (!application.good()) {
        throw OpenApplicationImage(path, "getTimestamp: failed to read timestamp data");
    }

    return timestamp;
}

void applicationImage::read_img_content_only(std::function<void(char *, uint32_t)> func, uint64_t content_size)
{
    application.seekg(this->header_size, application.beg);
    char BUFFER[FILE_CHUNK_BUFFER] = {0};

    uint64_t bytes_read = 0;
    while (bytes_read < content_size)
    {
        uint32_t to_read = std::min(static_cast<uint64_t>(FILE_CHUNK_BUFFER), content_size - bytes_read);

        application.read(BUFFER, to_read);
        std::streamsize actually_read = application.gcount();

        if (actually_read == 0)
        {
            const std::string error_msg = "No more data to read but content_size not reached";
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, "read_img_content_only: " + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }

        func(BUFFER, static_cast<uint32_t>(actually_read));
        bytes_read += actually_read;

        if (actually_read < to_read)
        {
            // EOF reached before expected
            if (bytes_read < content_size)
            {
                const std::string error_msg = "Unexpected EOF reached during content read";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, "read_img_content_only: " + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
            break;
        }
    }
}
