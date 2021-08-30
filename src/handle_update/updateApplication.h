/**
 * Error classes and application update functionality.
 */
#pragma once

#include "../uboot_interface/UBoot.h"
#include "updateBase.h"
#include "applicationImage.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include <exception>
#include <string>
#include <memory>
#include <filesystem>


#define RAUC_SYSTEM_PATH "/etc/rauc/system.conf"
#define STANDARD_APP_IMG_STORE "/rw_fs/root/application/"
#define STANDARD_APP_IMG_TEMP_STORE "/tmp/application_package"
#define PATH_TO_APPLICATION_VERSION_FILE "/etc/app_version"

constexpr char APP_UPDATE[] = "application update";

namespace updater 
{
    ///////////////////////////////////////////////////////////////////////////
    /// applicationUpdate' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Base class of for all exceptions that happens during installation.
     */
    class ErrorApplicationInstall : public fs::BaseFSUpdateException
    {
        public:
            ErrorApplicationInstall() = default;
    };

    class WrongApplicationPart : public ErrorApplicationInstall
    {
        public:
            /**
             * Error during read variable. Formward error.
             * @param erro Formward error message.
             */
            explicit WrongApplicationPart(const std::string &err)
            {
                this->error_msg = err;
            }
    };

    class Openx509Certificate : public ErrorApplicationInstall
    {
        public:
            /**
             * Could not parse x509 certificate.
             * @param path Path to x509 certificate.
             * @param error Error of parsing certificate.
             */ 
            Openx509Certificate(const std::filesystem::path &path, const std::string &error)
            {
                this->error_msg = std::string("Error: ") + error + std::string("; ");
                this->error_msg += std::string("Path: ") + path.string();
            }
    };

    class ReadPointOfTime : public ErrorApplicationInstall
    {
        public:
            /**
             * Can not read timestring of application image.
             * @param time_string Wrong timestring.
             */
            explicit ReadPointOfTime(const std::string &time_string)
            {
                this->error_msg = std::string("Error reading timestring: \"") + time_string;
                this->error_msg += std::string("\"");
            }
    };

    class DuringCopyFile : public ErrorApplicationInstall
    {
        public:
            /**
             * Can not extract application image from application package.
             * @param source Source of application update package.
             * @param dest Destination of application image.
             */
            DuringCopyFile(const std::string &source, const std::string &dest)
            {
                this->error_msg = std::string("Can not copy \"") + source + std::string("\" to ");
                this->error_msg += dest + std::string("\"");
            }
    };

    class SignatureMismatch : public ErrorApplicationInstall
    {
        public:
            /**
             * Application signature mismatch. Signature of application does not match with application and certificate.
             * @param path_to_bundle Path to application bundle.
             */
            explicit SignatureMismatch(const std::string &path_to_bundle)
            {
                this->error_msg = std::string("Application image signature missmatch: ") + path_to_bundle;
            }
    };

    class GetApplicationVersion : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not read application version.
             * @param path_to_version_file Path to application version file.
             * @param error_msg Reason for error.
             */
            GetApplicationVersion(const std::string & path_to_version_file, const std::string & error_msg)
            {
                this->error_msg = std::string("Could not get application version; path: \"") + path_to_version_file;
                this->error_msg += std::string("\" error message: ") + error_msg; 
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
            bool x509_verify_application_bundle(applicationImage &);

        public:
            /**
             * Create application update object.
             * @param ptr UBoot reference object.
             * @param logger Logger reference object.
             */
            applicationUpdate(const std::shared_ptr<UBoot::UBoot> &, const std::shared_ptr<logger::LoggerHandler> &);
            ~applicationUpdate();

            applicationUpdate(const applicationUpdate &) = delete;
            applicationUpdate &operator=(const applicationUpdate &) = delete;
            applicationUpdate(applicationUpdate &&) = delete;
            applicationUpdate &operator=(applicationUpdate &&) = delete;

            /**
             * Install application function, override install function derived from updateBase
             * @param path_to_bundle Path to application bundle.
             * @throw ErrorApplicationInstall Base class for all errors that can occur during installation process.
             * @throw SignatureMismatch Signature of certificate missmatch with application image.
             * @throw DuringCopyFile Can not copy application image of application bundle to destination.
             * @throw ErrorReadPointOfTime Can not signdature timestring of application image.
             * @throw Openx509Certificate Can not read x509 certificate or certificate is invalid.
             * @throw WrongApplicationPart Can not read variable.
             */
            void install(const std::filesystem::path &) override;

            /**
             * Rollback, override derived from updateBase rollback function.
             * @throw WrongApplicationPart Can not read application variable.
             */
            void rollback() override;

            /**
             * Read application version.
             * @return Numeric value of current application version.
             * @throw GetApplicationVersion Can not read application version.
             */
            unsigned int getCurrentVersion() override;
    };
}