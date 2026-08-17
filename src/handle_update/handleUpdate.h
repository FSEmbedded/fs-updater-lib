/**
 * Read system variables to get current update state.
 */

#pragma once

#include "updateDefinitions.h"
#include "../uboot_interface/IUBootEnv.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include "updater_exceptions.h"

#include <memory>
#include <exception>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

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
            std::shared_ptr<UBoot::IUBootEnv> uboot_handler;
            std::shared_ptr<logger::LoggerHandler> logger;

            const std::vector<update_definitions::Flags> get_complete_update(bool next_state);

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

            /**
             * Index of the single firmware digit recording an install in
             * flight, or -1 if there is none or more than one.
             */
            int32_t uncommitted_fw_index(const std::vector<uint8_t> &update_bits);

            /**
             * Did the install's activation never happen? Reachable when power
             * is lost between the install's env write and the bootloader
             * backend taking the target slot out of the rotation. The three
             * reboot predicates all require the two boot orders to differ, so
             * without this the state has no verb that leaves it.
             */
            bool install_never_activated(const std::string &boot_order_old,
                const std::string &boot_order);

        public:
            /**
             * Bootstate constructor.
             * @param ptr U-Boot environment access.
             * @param logger Logger reference.
             */
            Bootstate(const std::shared_ptr<UBoot::IUBootEnv> & ptr, const std::shared_ptr<logger::LoggerHandler> & logger);
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
            bool pendingUpdateRollback(update_definitions::UBootBootstateFlags & update_reboot_state,
                                      const std::string &sysfs_block_root = "/sys/class/block");
            
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
            void confirmPendingApplicationUpdate(const std::string &sysfs_block_root = "/sys/class/block");

            /**
             * Confirm pending application update.
             * @throw FirmwareRebootStateNotDefined A firmware reboot state is not defined.
             * @throw ConfirmPendingFirmwareApplicationUpdate If a pending application & firmware update is not stated a pending application & firmware update can not be confirmed.
             */
            void confirmPendingApplicationFirmwareUpdate();

            /**
             * Confirm pending update rollback.
             * @throw confirmUpdateRollback If a failure occcurs, during the committing process.
             */
            void confirmUpdateRollback();

            /**
             * Check if a update process is currently running.
             * @return Boolean state.
             */
            bool noUpdateProcessing();

            /**
             * Perform firmware rollback of an uncommited firmware update. 
             */
            void firmware_rollback();

            /**
             * Perform application rollback of an uncommited application update.
             * @param app_rollback Callable flipping the application slot selection.
             */
            void applicaton_rollback(const std::function<void()> &app_rollback,
                                    const std::string &sysfs_block_root = "/sys/class/block");

            /**
             * What the loop-device scan says about the mounted app image.
             * A third state exists because pre-mount (e.g. early boot,
             * before the app image is loop-mounted) zero loop devices
             * exist regardless of reboot state - the question "did the
             * reboot land?" is unanswerable there, not an error.
             */
            enum class AppImageState : unsigned char
            {
                ACTIVE_SLOT_MOUNTED, /* loop-mounted image matches env 'application' */
                OTHER_SLOT_MOUNTED,  /* an app image is mounted, but the other slot */
                NOT_MOUNTED          /* no loop device carries an app image (e.g. pre-mount) */
            };

            /**
             * Which app slot's image is loop-mounted, relative to the env
             * 'application' variable. Scans every loop* device under
             * sysfs_block_root for a backing file matching the expected
             * app slot - not only loop0, since another consumer of the
             * loop-device pool can grab loop0 first.
             * @param sysfs_block_root Overridable for tests; production
             *        default is the real sysfs block-device root.
             * @throw GetLoopDevices Only when sysfs_block_root itself is
             *        unreadable; zero loop devices is NOT_MOUNTED, not an
             *        error.
             */
            AppImageState application_reboot(const std::string &sysfs_block_root = "/sys/class/block");

            /**
             * Where the firmware boot landed relative to the staged order.
             */
            enum class FwRebootOutcome : unsigned char
            {
                PENDING,          /* preferred slot not booted yet */
                BOOTED_PREFERRED, /* running the slot BOOT_ORDER prefers */
                REVERTED          /* fell back to the old slot; a boot budget is drained */
            };

            /**
             * Classify the current boot against BOOT_ORDER / BOOT_ORDER_OLD and
             * the boot budgets. Single source of truth for "did the staged
             * order's reboot land, revert, or not happen yet".
             */
            FwRebootOutcome classify_fw_reboot();

            /**
             * Is firmware reboot successful
             */
            bool firmware_reboot();

            /**
             * Does the durable state record a firmware install that was
             * interrupted before its target was ever activated? Lets a caller
             * tell that window apart from a pending update that is merely
             * awaiting the application's verdict, since both report the same
             * pending state.
             */
            bool stalled_install_pending();
            /**
             * Get bit number for firmware or application of update environment.
             */
            int32_t get_update_bit(update_definitions::Flags flag, bool next);
    };
}
