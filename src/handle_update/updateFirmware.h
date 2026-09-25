/**
 * Error classes and firmware update functionality.
 */

#pragma once

#include "updateBase.h"

#include "../uboot_interface/UBoot.h"

#include "../rauc/rauc_handler.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include "updater_exceptions.h"

#include <exception>
#include <string>
#include <memory>
#include <fstream>

inline constexpr const char* PATH_TO_FIRMWARE_VERSION_FILE = "/etc/fw_version";
constexpr char FIRMWARE_UPDATE[] = "firmware update";


namespace updater 
{  
    ///////////////////////////////////////////////////////////////////////////
    /// firmwareUpdate declaration
    ///////////////////////////////////////////////////////////////////////////
    class firmwareUpdate : public updateBase
    {
        private:
            rauc::rauc_handler system_installer;

        public:

            /**
             * Create firmware update object. Use reference from UBoot and Loggerhandler object.
             * @param ptr UBoot::UBoot reference.
             * @param logger logger::LoggerHandler reference.
             */
            firmwareUpdate(const std::shared_ptr<UBoot::UBoot> &, const std::shared_ptr<logger::LoggerHandler> &);

            ~firmwareUpdate();

            firmwareUpdate(const firmwareUpdate &) = delete;
            firmwareUpdate &operator=(const firmwareUpdate &) = delete;
            firmwareUpdate(firmwareUpdate &&) = delete;
            firmwareUpdate &operator=(firmwareUpdate &&) = delete;

            /**
             * Install firmware object for given path.
             * @param path_to_bundle Path to RAUC artifact.
             * @throw FirmwareUpdateInstall Error when error occurs during installation.
             */
            void install(const std::string &) override;

            /**
             * Rolllback from current state to former.
             * @throw FirmwareRollback When rollback is not possible or failed.
             */
            void rollback() override;

            /**
             * Return current firmware version.
             * @return Firmware version as number.
             * @throw GetFirmwareVersion When current version can not be read or parsed.
             */
            version_t getCurrentVersion() override;

            /**
             * Return if current RAUC state is a failed update or not.
             * @return Return boolean value of failed firmware update or not.
             * @throw WrongVariableContent Variable content missmatch to the expected one.
             */
            bool failedUpdateReboot();
    };
}
