#include "updateApplication.h"

updater::applicationUpdate::applicationUpdate(const std::shared_ptr<UBoot::UBoot> & ptr):
    updateBase(ptr),
    application_image_path(STANDARD_APP_IMG_STORE),
    application_temp_path(STANDARD_APP_IMG_TEMP_STORE)

{
    inicpp::config rauc_config = inicpp::parser::load_file(RAUC_SYSTEM_PATH);
    this->path_to_cert = std::string("/etc/rauc/") + rauc_config["keyring"]["path"].get<inicpp::string_ini_t>();
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
        const long application_img_size = file_size - length_of_sign - SIZE_CERT_APP_IMG - SIZE_CERT_APP_DATE_SIGN;
        
        char signature[length_of_sign+1] = {0};
        char time_of_sign[SIZE_CERT_APP_DATE_SIGN+1] = {0};

        if( (application_image.end - SIZE_CERT_APP_IMG - SIZE_CERT_APP_DATE_SIGN - length_of_sign) > 0)
        {
            throw(ErrorOpenApplicationImage(path, "Size of signature is longer than the whole file"));
        }

        application_image.seekg( -1 * (SIZE_CERT_APP_IMG + length_of_sign), application_image.end);
        application_image.read(signature, length_of_sign);

        application_image.seekg( -1 * (SIZE_CERT_APP_IMG + SIZE_CERT_APP_DATE_SIGN + length_of_sign), application_image.end);
        application_image.read(time_of_sign, SIZE_CERT_APP_DATE_SIGN);
        application_image.seekg(0, application_image.beg);

        struct tm time_of_signing;
        if(!strptime(time_of_sign, "%Y-%m-%dT%H:%M:%S.%f", &time_of_signing))
        {
            throw(ErrorReadPointOfTime(std::string(time_of_sign)));
        }

        const std::chrono::system_clock::time_point signing = std::chrono::system_clock::from_time_t(mktime(&time_of_signing));
        application_image.seekg(0, application_image.beg);

        Botan::X509_Certificate certificate(this->path_to_cert);
        Botan::X509_Time current_timepoint(signing);

        if ((certificate.not_before() >= current_timepoint) && (certificate.not_after() <= current_timepoint))
        {
            ErrorOpenx509Certificate(path, "Certificate is expired");
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
            throw(ErrorOpenApplicationImage(path,"End-of-File reached on input operation"));
        }
        else if (application_image.fail())
        {
            throw(ErrorOpenApplicationImage(path,"Logical error on i/o operation"));
        }
        else if (application_image.bad())
        {
            throw(ErrorOpenApplicationImage(path,"Read/writing error on i/o operation"));
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
            throw(ErrorGetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE,"End-of-File reached on input operation"));
        }
        else if (application_version.fail())
        {
            throw(ErrorGetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE,"Logical error on i/o operation"));
        }
        else if (application_version.bad())
        {
            throw(ErrorGetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE,"Read/writing error on i/o operation"));
        }
    }

    if (std::regex_match(app_version, file_content_regex))
    {
        current_app_version = std::stoul(app_version);
    }
    else
    {
        std::string error_msg("Content miss formatting rules: ");
        error_msg += app_version;
        throw(ErrorGetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
    }

    return current_app_version;
}

