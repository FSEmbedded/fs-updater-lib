#pragma once
// Lightweight updater:: exception definitions, split out of handleUpdate.h and
// updateFirmware.h so consumers (and the classification test) can use them without
// pulling <libuboot.h> / json / the engine headers.
#include "../BaseException.h"
#include "fs_exceptions.h"
#include <string>

namespace updater
{
    ///////////////////////////////////////////////////////////////////////////
    /// Bootstate' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    class GetLoopDevices : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not get mounted loop devices.
             * @param msg Error related message.
             */
            explicit GetLoopDevices(const std::string &msg)
            {
                this->error_msg = std::string("Could not get mounted loop devices: ") + msg;
            }
    };

    class ConfirmPendingFirmwareUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm pending firmware update.
             * @param msg Error message.
             */
            explicit ConfirmPendingFirmwareUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Pending firmware update cannot be confirmed: ") + msg;
            }
    };

    class ConfirmPendingApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm pending application update.
             * @param msg Error message.
             */
            explicit ConfirmPendingApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Pending application update cannot be confirmed: ") + msg;
            }
    };

    class ConfirmFailedFirmwareUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm failed firmware update.
             * @param msg Error message.
             */
            explicit ConfirmFailedFirmwareUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Failed firmware update cannot be confirmed: ") + msg;
            }
    };

    class ConfirmFailedRebootFirmwareUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm failed reboot firmware update.
             * @param msg Error message.
             */
            explicit ConfirmFailedRebootFirmwareUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Failed reboot firmware update cannot be confirmed: ") + msg;
            }
    };

    class ConfirmFailedApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm failed application update.
             * @param msg Error message.
             */
            explicit ConfirmFailedApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Failed application update cannot be confirmed: ") + msg;
            }
    };

    class FirmwareRebootStateNotDefined : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Firmware reboot state is not defined.
             */
            FirmwareRebootStateNotDefined()
            {
                this->error_msg = std::string("This error state is not defined in a firmware update process!");
            }
    };

    class ConfirmPendingFirmwareApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm pending firmware and application update.
             * @param msg Error message.
             */
            explicit ConfirmPendingFirmwareApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Pending firmware & application update cannot be confirmed: ") + msg;
            }
    };

    /* A specialization of NotAllowedUpdateState, not a plain
     * BaseFSUpdateException sibling of it: the caller is refused because the
     * current state does not allow the operation, which is exactly what that
     * category exists to classify. Deriving from it directly (rather than
     * from BaseFSUpdateException) is what makes classify_active_exception()
     * report it as not_allowed_state instead of collapsing it into the
     * generic internal bucket. */
    class MissingReboot : public fs::NotAllowedUpdateState
    {
        public:
            /**
             * Reboot required before commit can proceed.
             * @param msg Error message.
             */
            explicit MissingReboot(const std::string &msg)
            {
                this->error_msg = std::string("Reboot required before commit: ") + msg;
            }
    };

    /* A rollback or slot-switch verb was asked in an update state that owns
     * no such move. Derived from NotAllowedUpdateState for the same reason as
     * MissingReboot: the bus and the command line already report that
     * category as a state refusal. */
    class RollbackNotAllowed : public fs::NotAllowedUpdateState
    {
        public:
            /**
             * @param verb Refused operation, e.g. "firmware rollback".
             * @param state Durable update_reboot_state the verb met.
             * @param way_out What the caller can do to leave that state.
             */
            RollbackNotAllowed(const std::string &verb, const std::string &state, const std::string &way_out)
            {
                this->error_msg = verb + " is not allowed in update state " + state + ": " + way_out;
            }
    };

    class ConfirmMissedRebootDuringRollback : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm pending firmware robback reboot.
             * @param msg Error message.
             */
            ConfirmMissedRebootDuringRollback()
            {
                this->error_msg = std::string("Could not confirm missing reboot during rollback operation");
            }
    };

    class RebootStateNotInterpretable : public fs::BaseFSUpdateException
    {
        public:
            /**
             * update_reboot_state holds content that cannot be decoded; no
             * verb may overwrite it.
             */
            RebootStateNotInterpretable()
            {
                this->error_msg = std::string("update_reboot_state is not interpretable; rollback refused");
            }
    };

    class ConfirmPendingRollback : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Can not confirm pending firmware robback reboot.
             * @param msg Error message.
             */
            ConfirmPendingRollback()
            {
                this->error_msg = std::string("Could not confirm pending rollback");
            }
    };

    class RollbackFirmwareUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Cannot perform firmware rollback
             * @param msg Error message.
             */
            explicit RollbackFirmwareUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Error during firmware rollback: ") + msg;
            }
    };

    class RollbackApplicationUpdate : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Cannot perform application rollback
             * @param msg Error message.
             */
            explicit RollbackApplicationUpdate(const std::string & msg)
            {
                this->error_msg = std::string("Error during application rollback: ") + msg;
            }
    };

    class ReadCmdline : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Cannot read /proc/cmdline
             * @param msg Error msg
             */
            explicit ReadCmdline(const std::string & msg)
            {
                this->error_msg = std::string("Error during reading /proc/cmdline: ") + msg;
            }
    };

    ///////////////////////////////////////////////////////////////////////////
    /// firmwareUpdate' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    class FirmwareUpdateInstall : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Firmware update failed.
             * @param error_msg Report reason for failure.
             */
            explicit FirmwareUpdateInstall(const std::string & error_msg)
            {
                this->error_msg = std::string("Error during firmware update: ") + error_msg;
            }
    };

    class FirmwareRollback : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Rollback of firmware failed.
             * @param error_msg Report reason for failure.
             */
            explicit FirmwareRollback(const std::string & error_msg)
            {
                this->error_msg = std::string("Error during firmware rollback: ") + error_msg;
            }
    };

    class GetFirmwareVersion : public fs::BaseFSUpdateException
    {
        public:
            /**
             * Could to read current firmware version.
             * @param path_to_version_file File which contains the curret version string.
             * @param error_msg Report reason for failure.
             */
            GetFirmwareVersion(const std::string & path_to_version_file, const std::string & error_msg)
            {
                this->error_msg = std::string("Could not get firmware version; path: \"") + path_to_version_file;
                this->error_msg += std::string("\" error message: ") + error_msg; 
            }
    };

    class WrongVariableContent : public fs::BaseFSUpdateException
    {
        public:
            /**
             * UBoot variable does not fulfill expected logical content.
             * @param wrong_var Name of variable with wrong content.
             */
            explicit WrongVariableContent(const std::string & wrong_var)
            {
                this->error_msg = std::string("Wrong Variable content: \"") + wrong_var + std::string("\"");
            }
    };

    class RaucDetection : public fs::BaseFSUpdateException
    {
        public:
            /**
             * RAUC could not detect the active boot slot.
             */
            RaucDetection()
            {
                this->error_msg = std::string("Boot/Update slot could not be detected!");
            }
    };
}
