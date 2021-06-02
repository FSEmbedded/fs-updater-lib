#ifndef UPDATE_APPLICATION_H
#define UPDATE_APPLICATION_H

#include "../uboot_interface/UBoot.h"
#include "../rauc/rauc_handler.h"
#include "updateBase.h"

#include <inicpp/inicpp.h>
#include <botan/auto_rng.h>
#include <botan/hash.h>
#include <botan/hex.h>
#include <botan/x509cert.h>
#include <botan/pubkey.h>


#include <exception>
#include <string>
#include <memory>
#include <filesystem>
#include <regex>
#include <fstream>
#include <chrono>
#include <ctime>

#define RAUC_SYSTEM_PATH "/etc/rauc/system.conf"
#define STANDARD_APP_IMG_STORE "/rw_fs/root/application/"
#define STANDARD_APP_IMG_TEMP_STORE "/tmp/application_package"
#define PATH_TO_APPLICATION_VERSION_FILE "/etc/app_version"

#define SIZE_CERT_APP_IMG 1024
#define SIZE_CERT_APP_DATE_SIGN 26
#define FILE_CHUNK_BUFFER 512

namespace updater 
{
    ///////////////////////////////////////////////////////////////////////////
    /// applicationUpdate' exception definitions
    ///////////////////////////////////////////////////////////////////////////
    class ErrorApplicationInstall : public ErrorUpdateBaseException
    {
        public:
            ErrorApplicationInstall()
            {

            }
    };

    class ErrorInitSSL : public ErrorUpdateBaseException
    {
        public:
            ErrorInitSSL()
            {
                this->error_msg = std::string("Cannot init botan");
            }
    };

    class ErrorWrongApplicationPart : public ErrorUpdateBaseException
    {
        public:
            explicit ErrorWrongApplicationPart(const std::string &wrong_part)
            {
                this->error_msg = std::string("Wrong content of variable \"application\": ");
                this->error_msg += wrong_part;
            }
    };

    class ErrorOpenApplicationImage : public ErrorUpdateBaseException
    {
        public:
            ErrorOpenApplicationImage(const std::string &path, const std::string &error)
            {
                this->error_msg = std::string("Error: ") + error + std::string("; ");
                this->error_msg += std::string("Path: ") + path;
            }
    };

    class ErrorOpenx509Certificate : public ErrorUpdateBaseException
    {
        public:
            ErrorOpenx509Certificate(const std::string &path, const std::string &error)
            {
                this->error_msg = std::string("Error: ") + error + std::string("; ");
                this->error_msg += std::string("Path: ") + path;
            }
    };

    class ErrorReadPointOfTime : public ErrorUpdateBaseException
    {
        public:
            explicit ErrorReadPointOfTime(const std::string &time_string)
            {
                this->error_msg = std::string("Error reading timestring: \"") + time_string;
                this->error_msg += std::string("\"");
            }
    };

    class ErrorPublicKey : public ErrorUpdateBaseException
    {
        public:
            ErrorPublicKey(const std::string &path, const std::string &error)
            {
                this->error_msg = std::string("Error: ") + error + std::string("; ");
                this->error_msg += std::string("Path: ") + path;
            }
    };

    class ErrorDuringCopyFile : public ErrorUpdateBaseException
    {
        public:
            ErrorDuringCopyFile(const std::string &source, const std::string &dest)
            {
                this->error_msg = std::string("Can not copy \"") + source + std::string("\" to ");
                this->error_msg += dest + std::string("\"");
            }
    };

    class ErrorSignatureMissmatch : public ErrorUpdateBaseException
    {
        public:
            explicit ErrorSignatureMissmatch(const std::string &path_to_bundle)
            {
                this->error_msg = std::string("Application image signature missmatch: ") + path_to_bundle;
            }
    };

    class ErrorGetApplicationVersion : public ErrorUpdateBaseException
    {
        public:
            ErrorGetApplicationVersion(const std::string & path_to_version_file, const std::string & error_msg)
            {
                this->error_msg = std::string("Could not get application version; path: \"") + path_to_version_file;
                this->error_msg += std::string("\" error message: ") + error_msg; 
            }

    };
    class ErrorCouldNotRemoveWrongApplicationImage : public ErrorUpdateBaseException
    {
        public:
            explicit ErrorCouldNotRemoveWrongApplicationImage(const std::string & path)
            {
                this->error_msg = std::string("Could not remove application Image: \"") + path + std::string("\"");
            }

    };
    ///////////////////////////////////////////////////////////////////////////
    /// applicationUpdate declaration
    ///////////////////////////////////////////////////////////////////////////
    class applicationUpdate : public updateBase
    {   
        private:
            std::string path_to_cert;
            std::filesystem::path application_image_path, application_temp_path;
            bool x509_verify_application_bundle(const std::string &);
        public:
            explicit applicationUpdate(const std::shared_ptr<UBoot::UBoot> &);
            ~applicationUpdate();

            applicationUpdate(const applicationUpdate &) = delete;
            applicationUpdate &operator=(const applicationUpdate &) = delete;
            applicationUpdate(applicationUpdate &&) = delete;
            applicationUpdate &operator=(applicationUpdate &&) = delete;

            void install(const std::filesystem::path &);
            void rollback();
            unsigned int getCurrentVersion();
    };
};

#endif