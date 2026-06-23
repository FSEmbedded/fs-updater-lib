/**
 * Error classes and firmware update functionality.
 */

#pragma once

#include "updateBase.h"

#include "../uboot_interface/UBoot.h"

#include <fus_updater_lib/config.h>
#if BUILD_DBUS_SUPPORT
#include "../dbus/rauc_dbus_client.h"
#else
#include "../rauc/rauc_handler.h"
#endif

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
#if BUILD_DBUS_SUPPORT
            rauc::rauc_dbus_client system_installer;
#else
            rauc::rauc_handler system_installer;
#endif

        public:

            /**
             * Create firmware update object. Use reference from UBoot and Loggerhandler object.
             * @param ptr UBoot::UBoot reference.
             * @param logger logger::LoggerHandler reference.
             */
            firmwareUpdate(const std::shared_ptr<UBoot::UBoot> & /*ptr*/, const std::shared_ptr<logger::LoggerHandler> & /*logger*/);

            ~firmwareUpdate() override;

            firmwareUpdate(const firmwareUpdate &) = delete;
            firmwareUpdate &operator=(const firmwareUpdate &) = delete;
            firmwareUpdate(firmwareUpdate &&) = delete;
            firmwareUpdate &operator=(firmwareUpdate &&) = delete;

            /**
             * Install firmware object for given path.
             * @param path_to_bundle Path to RAUC artifact.
             * @throw FirmwareUpdateInstall Error when error occurs during installation.
             */
            void install(const std::string & /*unused*/) override;

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

            /**
             * Tell RAUC to mark the "other" (inactive) slot as good.
             * RAUC's U-Boot pengutronix bootselect group translates the
             * resulting `Mark("good","other")` into a BOOT_ORDER swap +
             * counter init so the next reboot lands on the new slot.
             *
             * Used by the Apply path after a successful firmware install
             * to commit the slot selection without performing the
             * post-reboot detection (which would mis-fire pre-reboot).
             *
             * @throw rauc::RaucMarkOtherPartition on RAUC D-Bus failure.
             */
            void markOtherPartition();
    };
}
