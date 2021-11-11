#include "updateApplication.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"

#include <inicpp/inicpp.h>
#include <botan/auto_rng.h>
#include <botan/hash.h>
#include <botan/hex.h>
#include <botan/x509cert.h>
#include <botan/pubkey.h>
#include <botan/pk_keys.h>
#include <botan/rng.h>

#include <regex>
#include <fstream>
#include <chrono>
#include <ctime>



updater::applicationUpdate::applicationUpdate(const std::shared_ptr<UBoot::UBoot> & ptr, const std::shared_ptr<logger::LoggerHandler> &logger):
    updateBase(ptr, logger),
    application_image_path(STANDARD_APP_IMG_STORE),
    application_temp_path(STANDARD_APP_IMG_TEMP_STORE)
{
    try
    {
        inicpp::config rauc_config = inicpp::parser::load_file(RAUC_SYSTEM_PATH);
        this->path_to_cert = std::string("/etc/rauc/") + rauc_config["keyring"]["path"].get<inicpp::string_ini_t>();

    }
    catch(const inicpp::exception& e)
    {
        throw std::runtime_error(std::string("Exception during reading from inicpp: ") + e.what());
    }

    this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, "applicationUpdate: constructor", logger::logLevel::DEBUG));

}

updater::applicationUpdate::~applicationUpdate() 
{
    this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, "applicationUpdate: deconstructor", logger::logLevel::DEBUG));

}

bool updater::applicationUpdate::x509_verify_application_bundle(applicationImage & application)
{
    // read the file
    std::chrono::system_clock::time_point signing = application.getTimeOfSigning();
    std::vector<uint8_t> signature = application.getSignature();
    bool retValue = false;
    try
    {
        Botan::X509_Certificate certificate(this->path_to_cert);
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: not allowed before: ") + certificate.not_before().to_string(), logger::logLevel::DEBUG));
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: not allowed after: ") + certificate.not_after().to_string(), logger::logLevel::DEBUG));

        std::unique_ptr<Botan::RandomNumberGenerator> rng(new Botan::AutoSeeded_RNG);
        std::unique_ptr<Botan::Public_Key> pub_key = certificate.load_subject_public_key();
        if(!pub_key->check_key(*rng.get(), false))
        {
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: loaded key is invalid") , logger::logLevel::ERROR));
            throw std::invalid_argument("Loaded key is invalid");
        }
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: used algorithm: ") + pub_key->algo_name(), logger::logLevel::DEBUG));

        Botan::X509_Time current_timepoint(signing);

        if ((certificate.not_before() >= current_timepoint) && (certificate.not_after() <= current_timepoint))
        {
            const std::string error_msg = "Certificate is expired";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, error_msg, logger::logLevel::ERROR));
            Openx509Certificate(application.getPath(), error_msg);
        }

        Botan::PK_Verifier verifier(*pub_key.get(), "EMSA4(SHA-256)", Botan::IEEE_1363);
        
        std::function<void(char *, uint32_t)> crypto_wrapper = [&](char * buffer, uint32_t buffer_length) {
            verifier.update((uint8_t *) buffer, buffer_length);
        };

        application.read_img(crypto_wrapper);
        const bool verfied_file = verifier.check_signature(signature);

        if (verfied_file)
        {
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: signature verified: ") + std::to_string(verfied_file), logger::logLevel::DEBUG));
            retValue = true;
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("x509_verify_application_bundle: signature verified: ") + std::to_string(verfied_file), logger::logLevel::ERROR));
            retValue = false;
        }
    }
    catch(const Botan::Exception &e)
    {
        throw BotanExecutionError(e.what());
    }
    return retValue;
}

void updater::applicationUpdate::install(const std::string & path_to_bundle)
{
    char current_app;
    try
    {
        current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
    }
    catch(const std::exception &err)
    {
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("install: could not get UBoot variable: ") + err.what(), logger::logLevel::ERROR));
        throw(WrongApplicationPart(err.what()));
    }

    this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("install: current app: ") + current_app, logger::logLevel::DEBUG));

    if (current_app == 'A')
    {
        this->application_image_path += std::string("app_b.squashfs");
    }
    else
    {
        this->application_image_path += std::string("app_a.squashfs");
    }

    applicationImage application(path_to_bundle, this->logger);

    if (!this->x509_verify_application_bundle(application))
    {
        throw(SignatureMismatch(path_to_bundle));
    }

    try
    {
        application.copyImage(this->application_image_path);

    }
    catch(...)
    {
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("install: could not copy image: ") + path_to_bundle, logger::logLevel::ERROR));
        throw(DuringCopyFile(path_to_bundle, this->application_image_path));
    }

    if(current_app == 'A')
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
    char current_app;
    try
    {
        current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
    }
    catch(const std::exception &err)
    {
        this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("rollback: could not get UBoot variable: ") + err.what(), logger::logLevel::ERROR));
        throw(WrongApplicationPart(err.what()));
    }

    if (current_app == 'A')
    {
        this->uboot_handler->addVariable("application", "B");
    }
    else
    {
        this->uboot_handler->addVariable("application", "A");
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
            throw(GetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
        }
        else if (application_version.fail())
        {
            const std::string error_msg = "Logical error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(GetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
        }
        else if (application_version.bad())
        {
            const std::string error_msg = "Read/writing error on I/O operation"; 
            this->logger->setLogEntry(logger::LogEntry(APP_UPDATE, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
            throw(GetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
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
        throw(GetApplicationVersion(PATH_TO_APPLICATION_VERSION_FILE, error_msg));
    }

    return current_app_version;
}

