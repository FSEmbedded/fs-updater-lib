#include "updateApplication.h"

updater::applicationUpdate::applicationUpdate(const std::shared_ptr<UBoot::UBoot> & ptr, const std::shared_ptr<logger::LoggerHandler> &logger):
    updateBase(ptr),
    application_image_path(STANDARD_APP_IMG_STORE),
    application_temp_path(STANDARD_APP_IMG_TEMP_STORE),
    logger(logger)
{
    inicpp::config rauc_config = inicpp::parser::load_file(RAUC_SYSTEM_PATH);
    this->path_to_cert = std::string("/etc/rauc/") + rauc_config["keyring"]["path"].get<inicpp::string_ini_t>();

    this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, "application Update constrcuted", logger::logLevel::DEBUG));

}

updater::applicationUpdate::~applicationUpdate() 
{
    this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, "application Update deconstrcuted", logger::logLevel::DEBUG));

}

bool updater::applicationUpdate::x509_verify_application_bundle(const std::string & path)
{
    std::ifstream application_image(path, std::ifstream::binary);
    if (application_image.good())
    {
        // read the file
        application_image.seekg(0, application_image.end);
        const unsigned int file_size = application_image.tellg();

        application_image.seekg(- SIZE_CERT_APP_IMG, application_image.end);

        char binary_length_of_sign[SIZE_CERT_APP_IMG + 1] = {0};
        application_image.read(binary_length_of_sign, SIZE_CERT_APP_IMG);
        
        const long length_of_sign = std::stoi(std::string(binary_length_of_sign));
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: length of signature in bytes: ") + std::to_string(length_of_sign), logger::logLevel::DEBUG));

        const long application_img_size = file_size - length_of_sign - SIZE_CERT_APP_IMG - SIZE_CERT_APP_DATE_SIGN;
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: application image size in bytes: ") + std::to_string(application_img_size), logger::logLevel::DEBUG));

        char signature[length_of_sign+1] = {0};
        char time_of_sign[SIZE_CERT_APP_DATE_SIGN+1] = {0};

        if( (application_image.end - SIZE_CERT_APP_IMG - SIZE_CERT_APP_DATE_SIGN - length_of_sign) > 0)
        {
            const std::string error_msg = "Size of signature is longer than the whole file";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorOpenApplicationImage(path, error_msg));
        }

        application_image.seekg( -1 * (SIZE_CERT_APP_IMG + length_of_sign), application_image.end);
        application_image.read(signature, length_of_sign);

        application_image.seekg( -1 * (SIZE_CERT_APP_IMG + SIZE_CERT_APP_DATE_SIGN + length_of_sign), application_image.end);
        application_image.read(time_of_sign, SIZE_CERT_APP_DATE_SIGN);
        application_image.seekg(0, application_image.beg);

        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: time of signing: ") + time_of_sign, logger::logLevel::DEBUG));

        struct tm time_of_signing;
        if(!strptime(time_of_sign, "%Y-%m-%dT%H:%M:%S.%f", &time_of_signing))
        {
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, "x509_verify_application_bundle: can not parse timestring", logger::logLevel::ERROR));
            throw(ErrorReadPointOfTime(std::string(time_of_sign)));
        }

        const std::chrono::system_clock::time_point signing = std::chrono::system_clock::from_time_t(mktime(&time_of_signing));
        application_image.seekg(0, application_image.beg);

        Botan::X509_Certificate certificate(this->path_to_cert);
        Botan::X509_Time current_timepoint(signing);

        if ((certificate.not_before() >= current_timepoint) && (certificate.not_after() <= current_timepoint))
        {
            const std::string error_msg = "Certificate is expired";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, error_msg, logger::logLevel::ERROR));
            ErrorOpenx509Certificate(path, error_msg);
        }

        Botan::PK_Verifier verifier(*certificate.load_subject_public_key().get(), "RSA(SHA-256)");
        char * BUFFER[FILE_CHUNK_BUFFER] = {0};
        while (application_image.tellg() < (application_img_size + SIZE_CERT_APP_DATE_SIGN)- FILE_CHUNK_BUFFER)
        {
            application_image.read((char *) BUFFER, FILE_CHUNK_BUFFER);
            verifier.update((uint8_t *) BUFFER, FILE_CHUNK_BUFFER);
        }
        application_image.read((char *) BUFFER, FILE_CHUNK_BUFFER);
        verifier.update((uint8_t *) BUFFER, FILE_CHUNK_BUFFER);

        const bool verfied_file = verifier.check_signature((uint8_t *) signature, length_of_sign);

        if (verfied_file)
        {
            application_image.close();
            std::filesystem::resize_file(path, application_img_size);
            return true;
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: signature verified: ") + std::to_string(verfied_file), logger::logLevel::ERROR));
            if (!std::filesystem::remove(path))
            {
                throw(ErrorCouldNotRemoveWrongApplicationImage(path));
            }
            return false;
        }
        
    }
    else
    {
        if(application_image.eof())
        {
            const std::string error_msg = "End-of-File reached on input operation";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorOpenApplicationImage(path, error_msg));
        }
        else if (application_image.fail())
        {
            const std::string error_msg = "Logical error on i/o operation";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE,std::string("x509_verify_application_bundle: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorOpenApplicationImage(path, error_msg));
        }
        else if (application_image.bad())
        {
            const std::string error_msg = "Read/writing error on i/o operation";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE,std::string("x509_verify_application_bundle: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorOpenApplicationImage(path, error_msg));
        }
    }
    return false;
}

void updater::applicationUpdate::install(const std::filesystem::path & path_to_bundle)
{
    const std::string current_app = this->uboot_handler->getVariable("application");

    if (current_app == "A")
    {
        this->application_image_path += std::filesystem::path("app_b.squashfs");
    }
    else if (current_app == "B")
    {
        this->application_image_path += std::filesystem::path("app_a.squashfs");
    }
    else
    {
        throw(ErrorWrongApplicationPart(current_app));
    }

    if (!this->x509_verify_application_bundle(path_to_bundle))
    {
        throw(ErrorSignatureMissmatch(path_to_bundle));
    }

    const bool copy_state = std::filesystem::copy_file(path_to_bundle, this->application_image_path);
    if (!copy_state)
    {

        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("install: Could not copy image: ") + path_to_bundle.string(), logger::logLevel::ERROR));
        throw(ErrorDuringCopyFile(path_to_bundle, this->application_image_path));
    }

    if(current_app == "A")
    {
        this->uboot_handler->addVariable("application", "B");
    }
    else
    {
        this->uboot_handler->addVariable("application", "A");
    }
}

void updater::applicationUpdate::rollback()
{
    const std::string current_app = this->uboot_handler->getVariable("application");

    if (current_app == "A")
    {
        this->uboot_handler->addVariable("application", "B");
    }
    else if (current_app == "B")
    {
        this->uboot_handler->addVariable("application", "A");
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, "rollback: could not get current application", logger::logLevel::ERROR));
        throw(ErrorWrongApplicationPart(current_app));
    }
}

unsigned int updater::applicationUpdate::getCurrentVersion()
{
    unsigned int current_app_version;
    std::regex file_content_regex("[0-9]{8}");
    std::string app_version;

    std::ifstream application_version(PATH_TO_APPLICATION_VERSION_FILE);
    if (application_version.good())
    {
        std::getline(application_version, app_version);
    }
    else
    {
        if(application_version.eof())
        {
            const std::string error_msg = "End-of-File reached on input operation";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorGetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
        }
        else if (application_version.fail())
        {
            const std::string error_msg = "Logical error on i/o operation";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorGetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
        }
        else if (application_version.bad())
        {
            const std::string error_msg = "Read/writing error on i/o operation"; 
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(ErrorGetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
        }
    }

    if (std::regex_match(app_version, file_content_regex))
    {
        current_app_version = std::stoul(app_version);
    }
    else
    {
        std::string error_msg("content miss formatting rules: ");
        error_msg += app_version;
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
        throw(ErrorGetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
    }

    return current_app_version;
}

