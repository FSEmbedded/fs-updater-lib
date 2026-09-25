/**
 * Read system variables to get current update state.
 */

#pragma once

#include "updateDefinitions.h"
#include "../uboot_interface/UBoot.h"

#include "updateApplication.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include "updater_exceptions.h"
#include "fs_consts.h"

#include <memory>
#include <exception>

constexpr char BOOTSTATE_DOMAIN[] = "bootstate";

/**
 * Namespace for all internal functions to create F&S Update functionality.
 */
namespace updater
{

    enum class CMDLINE_BOOTSTATE
    {
        PARTITION_A,
        PARTITION_B
    };

    ///////////////////////////////////////////////////////////////////////////
    /// Bootstate declaration
    ///////////////////////////////////////////////////////////////////////////
    class Bootstate
    {
        private:
            std::shared_ptr<UBoot::UBoot> uboot_handler;
            std::shared_ptr<logger::LoggerHandler> logger;
            /* Where the running application image is mounted from. */
            std::string loop_backing_file;
            /* Changes with every boot; stamps each update_reboot_state write. */
            std::string boot_id_file;

            std::string read_boot_id();

            const std::vector<update_definitions::Flags> get_complete_update(bool next_state);

            /* Whether an install completed since the last reboot. */
            bool install_pending();

            /* Stage the firmware part of a rollback commit on "update".
             * Returns true when a switch away from the running slot did not hold. */
            bool commit_running_firmware_slot(std::vector<uint8_t> &update);

            bool firmware_update_reboot_failed(const std::string &current_slot,
                const std::string &boot_order_old,
                const std::string &boot_order,
                const uint8_t &number_of_tries_a,
                const uint8_t &number_of_tries_b);

            bool firmware_update_reboot_successful(const std::string &current_slot,
                const std::string &boot_order_old,
                const std::string &boot_order);

            bool missing_firmware_update_reboot(const std::string &current_slot,
                const std::string &boot_order_old,
                const std::string &boot_order,
                const uint8_t &number_of_tries_a,
                const uint8_t &number_of_tries_b);

        public:
            /**
             * Bootstate constructor.
             * @param ptr UBoot reference.
             * @param logger Logger reference.
             * @param loop_backing_file sysfs file naming the mounted application image.
             * @param boot_id_file File holding an id unique to the running boot.
             */
            Bootstate(const std::shared_ptr<UBoot::UBoot> &ptr, const std::shared_ptr<logger::LoggerHandler> &logger,
                      std::string loop_backing_file = config::APP_LOOP_BACKING_FILE,
                      std::string boot_id_file = "/proc/sys/kernel/random/boot_id");
            ~Bootstate();

            Bootstate(const Bootstate &) = delete;
            Bootstate &operator=(const Bootstate &) = delete;
            Bootstate(Bootstate &&) = delete;
            Bootstate &operator=(Bootstate &&) = delete;

            /**
             * Detect if an application update is pending.
             * @return Boolean state.
             */
            bool pendingApplicationUpdate();

            /**
             * Detect if a firmware update is pending.
             * @return Boolean state.
             */
            bool pendingFirmwareUpdate();

            /**
             * Detect if a firmware and an application update is pending.
             * @return Boolean state.
             */
            bool pendingApplicationFirmwareUpdate();

            /**
             * Detect an application install that stopped before it named the
             * written slot: the uncommitted digit sits on the other slot and
             * "application" still names the running one.
             * @return Boolean state.
             */
            bool app_install_unnamed();

            /**
             * Detect if a firmware update is failed.
             * @return Boolean state.
             */
            bool failedFirmwareUpdate();

            /**
             * Detect if a firmware update reboot is failed.
             * @return Boolean state.
             */
            bool failedRebootFirmwareUpdate();

            /**
             * Detect if an application update is failed.
             * @return Boolean state.
             */
            bool failedApplicationUpdate();

            /**
             * Detect if a firmware rollback pending.
             * @return Boolean  
             */
            bool pendingFirmwareRollback();

            /**
             * Detect if a application rollback is pending.
             * @return Boolean  
             */
            bool pendingUpdateRollback(update_definitions::UBootBootstateFlags & update_reboot_state);

            /**
             * Detect whether update_reboot_state names a rollback lifecycle
             * already under way (reboot pending or already committable),
             * independent of whether its own reboot has happened. For
             * refusing a second switch/rollback while one is outstanding;
             * pendingUpdateRollback() answers the reboot-aware question
             * instead and is not interchangeable with this one.
             * @return Boolean
             */
            bool rollbackInProgress(const update_definitions::UBootBootstateFlags &update_reboot_state);

            /**
             * Confirm failed firmware update.
             * @throw ConfirmFailedFirmwareUpdate If a failed firmware update is not stated a failed firmware update can not be confirmed.
             */
            void confirmFailedFirmwareUpdate();

            /**
             * Confirm failed update reboot.
             * @throw ConfirmFailedRebootFirmwareUpdate If a failed firmware update reboot is not stated a failed update reboot can not be confirmed.
             */
            void confirmFailedRebootFirmwareUpdate();

            /**
             * Confirm failed application update.
             * @throw ConfirmFailedApplicationUpdate If a failed application update is not stated a failed application update can not be confirmed.
             */
            void confirmFailedApplicationeUpdate();

            /**
             * Confirm pending firmware update.
             * @throw ConfirmPendingFirmwareUpdate If a pending firmware update is not stated a pending firmware update can not be confirmed.
             * @throw FirmwareRebootStateNotDefined A firmware reboot state is not defined.
             */
            void confirmPendingFirmwareUpdate();

            /**
             * Confirm pending application update.
             * @throw ConfirmPendingApplicationUpdate If a pending application update is not stated a pending application update can not be confirmed.
             * @throw GetLoopDevices Can not get loop devie of application image. 
             */
            void confirmPendingApplicationUpdate();

            /**
             * Confirm pending application update.
             * @throw FirmwareRebootStateNotDefined A firmware reboot state is not defined.
             * @throw ConfirmPendingFirmwareApplicationUpdate If a pending application & firmware update is not stated a pending application & firmware update can not be confirmed.
             */
            void confirmPendingApplicationFirmwareUpdate();

            /**
             * Confirm pending update rollback.
             * @throw confirmUpdateRollback If a failure occcurs, during the committing process.
             * @return true if a requested slot switch did not hold and the running slot stays.
             */
            bool confirmUpdateRollback();

            /**
             * Check if a update process is currently running.
             * @return Boolean state.
             */
            bool noUpdateProcessing();

            /**
             * Check the "update" digits directly, independent of
             * update_reboot_state: none of the four may be uncommitted.
             * @return Boolean state.
             */
            bool updateDigitsAllCommitted();

            /**
             * Explain which "update" digits are still uncommitted and how to get past them,
             * for the refusal a new install gets while one is left over.
             * @return One sentence naming every affected slot.
             */
            std::string uncommittedDigitsHint();

            enum class RebootSinceStateWrite
            {
                YES,
                NO,
                UNKNOWN
            };

            /**
             * Stage update_reboot_state together with its stamp, the boot id
             * of the write. Every write of update_reboot_state goes through here.
             * @param flag State to stage.
             * @param keep_boot_id Keep the boot id of the stamp being replaced:
             * for a write that moves a state along without owing a new reboot.
             */
            void stage_update_reboot_state(update_definitions::UBootBootstateFlags flag, bool keep_boot_id = false);

            /**
             * Whether the system rebooted since update_reboot_state was written.
             * @return UNKNOWN when the stamp is missing, malformed or belongs to
             * another state, e.g. written by an fs-updater without stamps; the
             * caller then decides from the boot variables.
             */
            RebootSinceStateWrite rebooted_since_state_write();

            /**
             * Refuse any rollback while an install waits for its reboot.
             * Every rollback entry point calls this before it decides which
             * kind of rollback to perform.
             * @throw MissingReboot An install has completed since the last
             * reboot, or a combined install left the firmware without its reboot.
             */
            void refuse_rollback_before_reboot();

            /**
             * Perform firmware rollback of an uncommited firmware update.
             */
            void firmware_rollback();

            /**
             * Perform application rollback of an uncommited application update.
             */
            void applicaton_rollback(updater::updateBase &app_updater);

            /**
             * Is application reboot successful
             */
            bool application_reboot();

            /**
             * Is firmware reboot successful
             */
            bool firmware_reboot();
            /**
             * Get bit number for firmware or application of update environment.
             */
            int32_t get_update_bit(update_definitions::Flags flag, bool next);
    };
}
