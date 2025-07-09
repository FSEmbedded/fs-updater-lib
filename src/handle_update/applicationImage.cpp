#include "applicationImage.h"

extern "C" {
    #include "zlib.h"
}

#include <iterator>
#include <sstream>
#include <chrono>
#include <ctime>
#include <limits>

applicationImage::applicationImage(const std::string & path, const std::shared_ptr<logger::LoggerHandler> & logger):
    path(path),
    logger(logger),
    header_size(4+8+4)
{
    this->application = std::ifstream(this->path, std::ifstream::binary);

    if (!application.good())
    {
        if(application.eof())
        {
            const std::string error_msg = "End-of-File reached on input operation";
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("constructor: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        else if (application.fail())
        {
            const std::string error_msg = "Logical error on I/O operation";
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION,std::string("constructor: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        else if (application.bad())
        {
            const std::string error_msg = "Read/writing error on I/O operation";
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION,std::string("constructor: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
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

std::chrono::system_clock::time_point applicationImage::getTimeOfSigning()
{
    application.seekg(this->application_image_size + header_size, application.beg);
    char time_of_sign[SIZE_CERT_APP_DATE_SIGN + 1] = {0};
    application.read(time_of_sign, SIZE_CERT_APP_DATE_SIGN);
    if (!application.good())
    {
        if(application.eof())
        {
            const std::string error_msg = "End-of-File reached on output operation";
            this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getTimeOfSigning: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        else if (application.fail())
        {
            const std::string error_msg = "Logical error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getTimeOfSigning: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        else if (application.bad())
        {
            const std::string error_msg = "Read/writing error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getTimeOfSigning: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
    }

    struct tm time_of_signing;
    if(strptime(time_of_sign, "%Y-%m-%dT%H:%M:%S", &time_of_signing) != &time_of_sign[19])
    {
        this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getTimeOfSigning: can not parse timestring: ") + std::string(time_of_sign), logger::logLevel::ERROR));
        throw(ReadPointOfTime(std::string(time_of_sign)));
    }
    this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getTimeOfSigning: timestring: ") + std::string(time_of_sign), logger::logLevel::DEBUG));
    return std::chrono::system_clock::from_time_t(mktime(&time_of_signing));
}

std::vector<uint8_t> applicationImage::getSignature()
{
    application.seekg(this->application_image_size + SIZE_CERT_APP_DATE_SIGN + header_size, application.beg);

    application.seekg(0, application.end);
    const uint64_t max_file_size = application.tellg();
    const uint64_t length_signature = max_file_size - uint64_t(this->header_size) - uint64_t(SIZE_CERT_APP_DATE_SIGN) - this->application_image_size ;

    this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getSignature: length of signature: ") + std::to_string(length_signature), logger::logLevel::DEBUG));
    application.seekg(uint64_t(this->header_size) + uint64_t(SIZE_CERT_APP_DATE_SIGN) + this->application_image_size, application.beg);

    std::vector<uint8_t> retValue(length_signature);
    application.read((char *)retValue.data(), length_signature);
    if (!application.good())
    {
        if(application.eof())
        {
            const std::string error_msg = "End-of-File reached on output operation";
            this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getSignature: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        else if (application.fail())
        {
            const std::string error_msg = "Logical error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getSignature: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        else if (application.bad())
        {
            const std::string error_msg = "Read/writing error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(APPLICATION, std::string("getSignature: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
    }

    return retValue;
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
            if(application.eof())
            {
                const std::string error_msg = "End-of-File reached on output operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("read_img: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
            else if (application.fail())
            {
                const std::string error_msg = "Logical error on I/O operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("read_img: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
            else if (application.bad())
            {
                const std::string error_msg = "Read/writing error on I/O operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("read_img: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
        }
        func(BUFFER, FILE_CHUNK_BUFFER);
    }
    uint32_t residual_data = this->header_size + this->application_image_size + SIZE_CERT_APP_DATE_SIGN - application.tellg();
    application.read(BUFFER, std::streampos(residual_data));
    if (!application.good())
    {
        if(application.eof())
        {
            const std::string error_msg = "End-of-File reached on output operation";
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("read_img: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        else if (application.fail())
        {
            const std::string error_msg = "Logical error on I/O operation";
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("read_img: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
        else if (application.bad())
        {
            const std::string error_msg = "Read/writing error on I/O operation";
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("read_img: ") + error_msg, logger::logLevel::ERROR));
            throw(OpenApplicationImage(path, error_msg));
        }
    }
    func(BUFFER, residual_data);
}


void applicationImage::copyImage(const std::string & dest)
{
    std::ofstream destination(dest, std::ifstream::binary);
    application.seekg(this->header_size, application.beg);

    try
    {
        uint64_t cursor;
        char BUFFER[FILE_CHUNK_BUFFER] = {0};
        for(cursor = 0; (cursor + FILE_CHUNK_BUFFER) <= this->application_image_size; cursor += FILE_CHUNK_BUFFER)    
        {
            application.read((char *) BUFFER, FILE_CHUNK_BUFFER);
            if (!application.good())
            {
                if(application.eof())
                {
                    const std::string error_msg = "End-of-File reached on input operation";
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                    throw(OpenApplicationImage(path, error_msg));
                }
                else if (application.fail())
                {
                    const std::string error_msg = "Logical error on I/O operation";
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                    throw(OpenApplicationImage(path, error_msg));
                }
                else if (application.bad())
                {
                    const std::string error_msg = "Read/writing error on I/O operation";
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                    throw(OpenApplicationImage(path, error_msg));
                }
            }

            destination.write(BUFFER, FILE_CHUNK_BUFFER);
            if (!destination.good())
            {
                if(destination.eof())
                {
                    const std::string error_msg = "End-of-File reached on output operation";
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                    throw(OpenApplicationImage(path, error_msg));
                }
                else if (destination.fail())
                {
                    const std::string error_msg = "Logical error on I/O operation";
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                    throw(OpenApplicationImage(path, error_msg));
                }
                else if (destination.bad())
                {
                    const std::string error_msg = "Read/writing error on I/O operation";
                    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                    throw(OpenApplicationImage(path, error_msg));
                }
            }
            destination.flush();
        }
        application.read((char *) BUFFER, this->application_image_size - cursor);
        if (!application.good())
        {
            if(application.eof())
            {
                const std::string error_msg = "End-of-File reached on input operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
            else if (application.fail())
            {
                const std::string error_msg = "Logical error on I/O operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
            else if (application.bad())
            {
                const std::string error_msg = "Read/writing error on I/O operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
        }

        destination.write(BUFFER, this->application_image_size - cursor);
        if (!destination.good())
        {
            if(destination.eof())
            {
                const std::string error_msg = "End-of-File reached on output operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
            else if (destination.fail())
            {
                const std::string error_msg = "Logical error on I/O operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
            else if (destination.bad())
            {
                const std::string error_msg = "Read/writing error on I/O operation";
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + error_msg, logger::logLevel::ERROR));
                throw(OpenApplicationImage(path, error_msg));
            }
        }
        destination.flush();
    }
    catch(const std::exception& e)
    {
        if (destination.is_open())
            destination.close();
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION,std::string("copyImage: copy image to persistent: ") + e.what(), logger::logLevel::ERROR));
        throw(DuringWriteApplicationImage(e.what()));
    }
    /* sync tmp.app file state with storage device */
    int ret = fsync(GetFileDescriptorCStyle(*destination.rdbuf()));
    /* close tmp.app file */
    destination.close();
    if(ret)
    {
        /* sync fails */
        std::string err_str = std::string("Sync file ") + dest + std::string("fails. Errno: ") + std::to_string(ret);
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(APPLICATION, std::string("copyImage: ") + err_str, logger::logLevel::DEBUG));
        throw(DuringWriteApplicationImage(err_str));
    }
}

/* This function get filedescriptor (fd) from opened ofstream.
 * The fd is needed by fsync function to sync data to the disc. It sould to
 * to be faster as call sync with popen.
 * TODO: Need to be correct in the feature. It is not good solution.
 */
int applicationImage::GetFileDescriptorCStyle(std::filebuf &filebuf)
{
    class my_filebuf : public std::filebuf
    {
      public:
      /* _M_file is protected member.
      */
        int handle()
        {
            return _M_file.fd();
        }
    };

    return static_cast<my_filebuf &>(filebuf).handle();
}
