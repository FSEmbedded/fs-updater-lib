#include "handleUpdate.h"

#include "../uboot_interface/allowed_uboot_variable_states.h"
#include "app_bundle_install.h"
#include "reboot_state.h"
#include "updateApplication.h"
#include "util/posix_utils.h"
#include "utils.h"

extern "C" {
#include <dirent.h>
}

#include <algorithm>
#include <fstream>

updater::Bootstate::Bootstate(const std::shared_ptr<UBoot::IUBootEnv> &ptr,
                              const std::shared_ptr<logger::LoggerHandler> &logger)
    : uboot_handler(ptr), logger(logger)
{
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, "bootstate: constructor", logger::logLevel::DEBUG));
}

updater::Bootstate::~Bootstate()
{
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, "bootstate: deconstruct", logger::logLevel::DEBUG));
}

const std::vector<update_definitions::Flags> updater::Bootstate::get_complete_update(bool next_state)
{
    std::vector<uint8_t> completed_update =
        util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
    std::vector<update_definitions::Flags> ret_value;

    int current_state = completed_update.at(this->get_update_bit(update_definitions::Flags::OS, next_state)) - '0';

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("get_complete_update: fw current_state ") + std::to_string(current_state),
        logger::logLevel::DEBUG));

    if ((current_state & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
    {
        ret_value.push_back(update_definitions::Flags::OS);
    }

    current_state = completed_update.at(this->get_update_bit(update_definitions::Flags::APP, next_state)) - '0';

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("get_complete_update: app current_state ") + std::to_string(current_state),
        logger::logLevel::DEBUG));

    if ((current_state & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
    {
        ret_value.push_back(update_definitions::Flags::APP);
    }

    return ret_value;
}

bool updater::Bootstate::pendingApplicationUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);

    if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) == update_state.end()) &&
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

        if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN,
        std::string("pendingApplicationUpdate: is an application update pending? ") + std::to_string(retValue),
        logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::pendingFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);

    if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) &&
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

        if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
        {
            retValue = true;
        }
    }

    /* After failed reboot U-Boot falls back to old slot.
     * Current slot FW bit is '0' so check above misses it.
     * Also check the next slot for uncommitted FW with state=2.
     */
    if (!retValue)
    {
        std::vector<update_definitions::Flags> next_state = this->get_complete_update(true);

        if ((std::find(next_state.begin(), next_state.end(), update_definitions::Flags::OS) != next_state.end()) &&
            (std::find(next_state.begin(), next_state.end(), update_definitions::Flags::APP) == next_state.end()))
        {
            const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

            if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
            {
                retValue = true;
            }
        }
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                                               std::string("pendingFirmwareUpdate: is a firmware update pending? ") +
                                                   std::to_string(retValue),
                                               logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::pendingApplicationFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);

    if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) &&
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

        if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
        {
            retValue = true;
        }
    }

    /* After failed reboot U-Boot falls back to old slot.
     * Current slot FW bit is '0' so check above misses it.
     * Check next slot for uncommitted FW and current/next for APP with state=4.
     */
    if (!retValue)
    {
        std::vector<update_definitions::Flags> next_state = this->get_complete_update(true);
        bool const os_next = std::find(next_state.begin(), next_state.end(), update_definitions::Flags::OS) != next_state.end();
        bool const app_current = std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end();
        bool const app_next = std::find(next_state.begin(), next_state.end(), update_definitions::Flags::APP) != next_state.end();

        if (os_next && (app_current || app_next))
        {
            const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

            if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
            {
                retValue = true;
            }
        }
    }

    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                         std::string("pendingApplicationFirmwareUpdate: is a firmware & application update pending? ") +
                             std::to_string(retValue),
                         logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::failedFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(true);

    if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) &&
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

        if (update_reboot_state == update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("failedFirmwareUpdate: is a firmware update failed? ") + std::to_string(retValue),
        logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::failedRebootFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);

    if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) &&
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

        if (update_reboot_state == update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN,
        std::string("failedRebootFirmwareUpdate: is a reboot firmware update failed? ") + std::to_string(retValue),
        logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::failedApplicationUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(true);

    if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) == update_state.end()) &&
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

        if (update_reboot_state == update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN,
        std::string("failedApplicationUpdate: is an application update failed? ") + std::to_string(retValue),
        logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::pendingFirmwareRollback()
{

    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("pendingUpdateRollback: UBootEnv: Var.:\"BOOT_ORDER_OLD\": ") + boot_order_old,
        logger::logLevel::DEBUG));
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("pendingUpdateRollback: UBootEnv: Var.:\"BOOT_ORDER\": ") + boot_order,
        logger::logLevel::DEBUG));

    const unsigned int number_of_tries_a =
        this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const unsigned int number_of_tries_b =
        this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                                               std::string("pendingUpdateRollback: BootEnv: Var.:\"BOOT_A_LEFT\": ") +
                                                   std::to_string(number_of_tries_a),
                                               logger::logLevel::DEBUG));
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                                               std::string("pendingUpdateRollback: BootEnv: Var.:\"BOOT_B_LEFT\": ") +
                                                   std::to_string(number_of_tries_b),
                                               logger::logLevel::DEBUG));

    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                                               std::string("pendingUpdateRollback: RAUC current slot: ") + current_slot,
                                               logger::logLevel::DEBUG));
    /* Check firmware reboot state after update */
    if (firmware_update_reboot_successful(current_slot, boot_order_old, boot_order) == true)
    {
        /* Reboot after rollback required */
        return false;
    }
    /* check reboot state after update fails rollback pending */
    /* true - means rollback pending and false is not */
    return firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                         number_of_tries_b);
}

bool updater::Bootstate::pendingUpdateRollback(update_definitions::UBootBootstateFlags &update_reboot_state,
                                               const std::string &sysfs_block_root)
{
    /* Check for incomplete state */
    if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK)
    {
        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Common update rollback pending"), logger::logLevel::DEBUG));
        return true;
    }
    if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Firmware update rollback pending"),
                                                   logger::logLevel::DEBUG));
        return true;
    }
    else if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_ROLLBACK)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Application update rollback pending"),
                                                   logger::logLevel::DEBUG));
        return true;
    }

    /* Possible that apply can't be reached and reboot for rollback pending.
     * In this case check for reboot state.
     */
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);
    if (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING)
    {
        bool pending = false;
        if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) !=
             update_state.end()) &&
            (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()))
        {
            pending = true;
        }
        else if (this->pendingFirmwareRollback() == true)
        {
            pending = true;
        }

        return pending;
    }
    if (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING)
    {
        bool pending = false;
        if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) !=
             update_state.end()) &&
            (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
        {
            pending = true;
        }
        else if (this->pendingFirmwareRollback() == true)
        {
            pending = true;
        }

        return pending;
    }
    else if (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING)
    {
        /* Derived, not decided again: classify_app_rollback owns this state's
         * evidence, and the status verb reports from the same call. Only a
         * reboot that has not happened yet leads out without a commit; the
         * other two outcomes both owe one. Keeping the derivation here rather
         * than duplicating the conditions is what stops the reported code and
         * the commit's precondition from drifting apart. */
        return this->classify_app_rollback(sysfs_block_root) != AppRollbackOutcome::REBOOT_OUTSTANDING;
    }

    return false;
}

void updater::Bootstate::confirmFailedFirmwareUpdate()
{
    if (this->failedFirmwareUpdate() == true)
    {
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        /* The update is over and the slot is condemned: settle first, then
         * mark. */
        const int32_t failed_fw = get_update_bit(update_definitions::Flags::OS, true);
        update.at(failed_fw) = digit_marked_bad(digit_settled(update.at(failed_fw)));

        const update_definitions::UBootBootstateFlags update_reboot_state =
            update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        write_update_bits(*this->uboot_handler, update);
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_reboot_state));

        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("confirmFailedFirmwareUpdate: failed firmware update is confirmed"),
            logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("confirmFailedFirmwareUpdate: no failed firmware update to confirm"),
            logger::logLevel::ERROR));
        throw(ConfirmFailedFirmwareUpdate("no failed firmware update detected"));
    }
}

void updater::Bootstate::confirmFailedRebootFirmwareUpdate()
{
    /* Recovery, not acknowledgement, and it takes no precondition.
     *
     * Nothing writes this state, so a device holding it got it from outside: an
     * environment edit, or a firmware old enough to have written it. Its meaning
     * is that the bootloader fell back to the proven slot -- which makes the slot
     * the device is running the committed one.
     *
     * The recovery claims only what is observable. The device booted the slot it
     * is running, so an uncommitted digit on that slot is settled. Nothing here
     * shows which slot failed to boot, so no slot is condemned and the boot order
     * is left as it stands. The budgets are put back because the pending state
     * gated the routine mark-good for as long as it lasted.
     */
    std::vector<uint8_t> update =
        util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
    const auto running_fw = get_update_bit(update_definitions::Flags::OS, false);

    /* "Uncommitted" is a bit, not the character '1': the validator counts every
     * digit with the low bit set, so '3' is as uncommitted as '1' is. Clear that
     * bit alone -- the slot booted, which is all this proves, and a bad mark it
     * carried in is not something the recovery has evidence against. */
    if (digit_in_flight(update.at(running_fw)))
    {
        update.at(running_fw) = digit_settled(update.at(running_fw));
        write_update_bits(*this->uboot_handler, update);
    }

    this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
    this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
    this->uboot_handler->addVariable(
        "update_reboot_state",
        update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN,
        std::string("confirmFailedRebootFirmwareUpdate: recovered from the failed-reboot state; "
                    "no slot condemned, boot order unchanged"),
        logger::logLevel::DEBUG));
}

void updater::Bootstate::confirmFailedApplicationeUpdate()
{
    if (this->failedApplicationUpdate() == true)
    {
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));

        const int32_t failed_app = get_update_bit(update_definitions::Flags::APP, true);
        update.at(failed_app) = digit_marked_bad(digit_settled(update.at(failed_app)));
        const update_definitions::UBootBootstateFlags update_reboot_state =
            update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        write_update_bits(*this->uboot_handler, update);
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_reboot_state));

        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("confirmFailedApplicationeUpdate: failed application update is confirmed"),
            logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("confirmFailedApplicationeUpdate: no failed application update to confirm"),
            logger::logLevel::ERROR));
        throw(ConfirmFailedApplicationUpdate("no failed application update detected"));
    }
}

void updater::Bootstate::confirmPendingFirmwareUpdate()
{
    if (this->pendingFirmwareUpdate())
    {
        const std::string boot_order_old =
            this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
        const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("confirmPendingFirmwareUpdate: UBootEnv: Var.:\"BOOT_ORDER_OLD\": ") + boot_order_old,
            logger::logLevel::DEBUG));
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: UBootEnv: Var.:\"BOOT_ORDER\": ") + boot_order,
            logger::logLevel::DEBUG));

        const unsigned int number_of_tries_a =
            this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
        const unsigned int number_of_tries_b =
            this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                             std::string("confirmPendingFirmwareUpdate: BootEnv: Var.:\"BOOT_A_LEFT\": ") +
                                 std::to_string(number_of_tries_a),
                             logger::logLevel::DEBUG));
        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                             std::string("confirmPendingFirmwareUpdate: BootEnv: Var.:\"BOOT_B_LEFT\": ") +
                                 std::to_string(number_of_tries_b),
                             logger::logLevel::DEBUG));

        const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
        const std::string current_slot = util::split(rauc_cmd, '=').back();

        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: RAUC current slot: ") + current_slot,
            logger::logLevel::DEBUG));

        if (this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                                number_of_tries_b))
        {
            this->logger->setLogEntry(
                std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update reboot failed, marking slot as bad"),
                                 logger::logLevel::ERROR));

            std::vector<uint8_t> update =
                util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
            const int32_t failed_fw = get_update_bit(update_definitions::Flags::OS, true);
            update.at(failed_fw) = digit_marked_bad(digit_settled(update.at(failed_fw)));
            write_update_bits(*this->uboot_handler, update);
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable(
                "update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else if (this->missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                                      number_of_tries_b))
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update reboot missing"),
                logger::logLevel::ERROR));
            throw(MissingReboot("firmware update requires reboot before commit"));
        }
        else if (this->firmware_update_reboot_successful(current_slot, boot_order_old, boot_order))
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update successful"),
                logger::logLevel::DEBUG));

            std::vector<uint8_t> update =
                util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
            const int32_t confirmed_fw = get_update_bit(update_definitions::Flags::OS, false);
            update.at(confirmed_fw) = digit_settled(update.at(confirmed_fw));
            write_update_bits(*this->uboot_handler, update);
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable(
                "update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else if (this->install_never_activated(boot_order_old, boot_order))
        {
            /* Nothing was staged to boot into, so no reboot is outstanding and
             * no fallback can have happened. Placed last so it can only claim
             * what would otherwise be the undefined-state throw. */
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN,
                std::string("confirmPendingFirmwareUpdate: install never activated, quarantining the interrupted slot"),
                logger::logLevel::ERROR));

            std::vector<uint8_t> update =
                util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
            /* Quarantine the slot carrying the uncommitted digit, not the one
             * get_update_bit() derives: that resolves against the running slot,
             * and once the proven slot's budget has eroded the device may
             * already be running the other one. Unlike a voluntary abandonment,
             * the write into the target was interrupted here, so a settled bad
             * mark is the honest record. */
            const int32_t interrupted_fw = this->uncommitted_fw_index(update);
            update.at(interrupted_fw) = digit_marked_bad(digit_settled(update.at(interrupted_fw)));
            write_update_bits(*this->uboot_handler, update);
            /* The mark-good gate withheld the counter reset for every boot this
             * state survived, so the running slot may be one boot away from
             * dropping out of the rotation. */
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable(
                "update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update state is illegal"),
                logger::logLevel::ERROR));
            throw(FirmwareRebootStateNotDefined());
        }
    }
    else
    {
        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: No firmware update pending"),
                             logger::logLevel::ERROR));
        throw(ConfirmPendingFirmwareUpdate("No pending firmware update"));
    }
}

void updater::Bootstate::confirmPendingApplicationUpdate(const std::string &sysfs_block_root)
{
    if (this->pendingApplicationUpdate())
    {
        const AppImageState app_image_state = this->application_reboot(sysfs_block_root);
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));

        if (app_image_state == AppImageState::ACTIVE_SLOT_MOUNTED)
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, "confirmPendingApplicationUpdate: mark application update as successful",
                logger::logLevel::DEBUG));
            const int32_t confirmed_app = get_update_bit(update_definitions::Flags::APP, false);
            update.at(confirmed_app) = digit_settled(update.at(confirmed_app));
            write_update_bits(*this->uboot_handler, update);
            this->uboot_handler->addVariable(
                "update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else if (app_image_state == AppImageState::OTHER_SLOT_MOUNTED)
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, "confirmPendingApplicationUpdate: missing reboot for application update",
                logger::logLevel::ERROR));
            throw(MissingReboot("application update requires reboot before commit"));
        }
        else
        {
            /* Not a missing reboot: the update never took effect at all.
             * Refuse the commit instead of blaming a reboot or silently
             * succeeding. */
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, "confirmPendingApplicationUpdate: no app image mounted",
                logger::logLevel::ERROR));
            throw(GetLoopDevices("no app image mounted; refusing to commit"));
        }
    }
}

void updater::Bootstate::confirmPendingApplicationFirmwareUpdate()
{
    if (this->pendingApplicationFirmwareUpdate())
    {
        const std::string boot_order_old =
            this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
        const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("confirmApplicationFirmwareUpdate: UBootEnv: Var.:\"BOOT_ORDER_OLD\": ") + boot_order_old,
            logger::logLevel::DEBUG));
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("confirmApplicationFirmwareUpdate: UBootEnv: Var.:\"BOOT_ORDER\": ") + boot_order,
            logger::logLevel::DEBUG));

        const uint8_t number_of_tries_a =
            this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
        const uint8_t number_of_tries_b =
            this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                             std::string("confirmApplicationFirmwareUpdate: BootEnv: Var.:\"BOOT_A_LEFT\": ") +
                                 std::to_string(number_of_tries_a),
                             logger::logLevel::DEBUG));
        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                             std::string("confirmApplicationFirmwareUpdate: BootEnv: Var.:\"BOOT_B_LEFT\": ") +
                                 std::to_string(number_of_tries_b),
                             logger::logLevel::DEBUG));

        const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
        const std::string current_slot = util::split(rauc_cmd, '=').back();

        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: RAUC current slot: ") + current_slot,
            logger::logLevel::DEBUG));

        if (this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                                number_of_tries_b))
        {
            const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
            std::vector<uint8_t> update =
                util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
            /* The install already flipped 'application' to the new slot, so
             * next=false resolves to the digit that went in flight -- and this
             * branch is about to flip it back, which makes that slot the
             * abandoned one. Computed before the flip below; the order matters. */
            const int32_t abandoned_app = get_update_bit(update_definitions::Flags::APP, false);
            const int32_t failed_fw = get_update_bit(update_definitions::Flags::OS, true);
            update.at(abandoned_app) = digit_settled(update.at(abandoned_app));
            update.at(failed_fw) = digit_marked_bad(digit_settled(update.at(failed_fw)));

            if (current_app == 'A')
            {
                this->uboot_handler->addVariable("application", "B");
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                    BOOTSTATE_DOMAIN,
                    "confirmApplicationFirmwareUpdate: application rollback to B during failed app & fw update",
                    logger::logLevel::DEBUG));
            }
            else
            {
                this->uboot_handler->addVariable("application", "A");
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                    BOOTSTATE_DOMAIN,
                    "confirmApplicationFirmwareUpdate: application rollback to A during failed app & fw update",
                    logger::logLevel::DEBUG));
            }

            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: firmware reboot failed, marking slot as bad"),
                logger::logLevel::ERROR));

            write_update_bits(*this->uboot_handler, update);
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable(
                "update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else if (this->missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                                      number_of_tries_b))
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: firmware update reboot missing"),
                logger::logLevel::ERROR));
            throw(MissingReboot("firmware & application update requires reboot before commit"));
        }
        else if (this->firmware_update_reboot_successful(current_slot, boot_order_old, boot_order))
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: firmware update successful"),
                logger::logLevel::DEBUG));

            std::vector<uint8_t> update =
                util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));

            const int32_t confirmed_fw = get_update_bit(update_definitions::Flags::OS, false);
            const int32_t confirmed_app = get_update_bit(update_definitions::Flags::APP, false);
            update.at(confirmed_fw) = digit_settled(update.at(confirmed_fw));
            update.at(confirmed_app) = digit_settled(update.at(confirmed_app));

            write_update_bits(*this->uboot_handler, update);
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable(
                "update_reboot_state",
                update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update state is illegal"),
                logger::logLevel::ERROR));
            throw(FirmwareRebootStateNotDefined());
        }
    }
    else
    {
        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: no firmware update pending"),
                             logger::logLevel::ERROR));
        throw(ConfirmPendingFirmwareApplicationUpdate("No pending firmware and application update"));
    }
}

void updater::Bootstate::confirmUpdateRollback()
{
    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Start rollback commit"), logger::logLevel::DEBUG));
    /* Two rollback shapes, told apart by the next slot's commit state:
     *  next uncommitted -> a pending update was rolled back: settle that slot to
     *                      committed and revert the boot order to the old slot.
     *  next committed   -> a switch to the other committed slot: adopt the new order.
     * The rolled-back slot is not marked bad here — U-Boot/preinit do not read the
     * update bitfield and RAUC leaves the slot good, so a bad mark would only desync
     * the two stores and block a later switch. A failed update reboot is marked bad
     * on its own path.
     *
     * The "next committed" shape is ambiguous while a rollback's reboot is still
     * outstanding: the uncommitted digit then belongs to the RUNNING slot, and that
     * slot is still the head of the boot order, so the operation reads as a switch
     * that landed and the order would be adopted — accepting the update the caller
     * asked to revert. Refused below, at the one place the misreading occurs.
     */
    /* Check for the last update reboot state */
    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);
    if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK ||
        update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("Commit firmware and application rollback."), logger::logLevel::DEBUG));
        const std::string boot_order_old =
            this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        /* check next state of update env. */
        if (!digit_in_flight(update.at(get_update_bit(update_definitions::Flags::OS, true))))
        {
            if (this->classify_fw_reboot() == FwRebootOutcome::BOOTED_PREFERRED)
            {
                /* Running slot still uncommitted: the rollback's reboot has not
                 * happened, so this is not a landed switch. */
                if (digit_in_flight(update.at(get_update_bit(update_definitions::Flags::OS, false))))
                {
                    throw(MissingReboot("firmware rollback requires reboot before commit"));
                }
                /* switch landed on the preferred slot: adopt the switched order */
                const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);
                this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            }
            else
            {
                /* switch boot never landed: keep the proven slot preferred and
                 * record the demonstrated boot failure on the dead slot */
                const int32_t dead_fw = get_update_bit(update_definitions::Flags::OS, true);
                update.at(dead_fw) = digit_marked_bad(digit_settled(update.at(dead_fw)));
                this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            }
        }
        else
        {
            update.at(get_update_bit(update_definitions::Flags::OS, true)) =
                digit_settled(update.at(get_update_bit(update_definitions::Flags::OS, true)));
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
        }
        /* Settle the uncommitted application slot to committed */
        if (digit_in_flight(update.at(get_update_bit(update_definitions::Flags::APP, true))))
        {
            update.at(get_update_bit(update_definitions::Flags::APP, true)) =
                digit_settled(update.at(get_update_bit(update_definitions::Flags::APP, true)));
        }
        write_update_bits(*this->uboot_handler, update);
        this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
        this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
        this->uboot_handler->addVariable(
            "update_reboot_state",
            update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
    }
    else if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK ||
             update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Firmware update rollback pending"),
                                                   logger::logLevel::DEBUG));
        const std::string boot_order_old =
            this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        /* check next state of update env. */
        if (!digit_in_flight(update.at(get_update_bit(update_definitions::Flags::OS, true))))
        {
            if (this->classify_fw_reboot() == FwRebootOutcome::BOOTED_PREFERRED)
            {
                /* Running slot still uncommitted: the rollback's reboot has not
                 * happened, so this is not a landed switch. */
                if (digit_in_flight(update.at(get_update_bit(update_definitions::Flags::OS, false))))
                {
                    throw(MissingReboot("firmware rollback requires reboot before commit"));
                }
                /* switch landed on the preferred slot: adopt the switched order */
                const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);
                this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            }
            else
            {
                /* switch boot never landed: keep the proven slot preferred and
                 * record the demonstrated boot failure on the dead slot */
                const int32_t dead_fw = get_update_bit(update_definitions::Flags::OS, true);
                update.at(dead_fw) = digit_marked_bad(digit_settled(update.at(dead_fw)));
                this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            }
        }
        else
        {
            update.at(get_update_bit(update_definitions::Flags::OS, true)) =
                digit_settled(update.at(get_update_bit(update_definitions::Flags::OS, true)));
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
        }
        write_update_bits(*this->uboot_handler, update);
        this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
        this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
        this->uboot_handler->addVariable(
            "update_reboot_state",
            update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
    }
    else if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_ROLLBACK ||
             update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Application update rollback pending"),
                                                   logger::logLevel::DEBUG));
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        /* Settle the uncommitted application slot to committed */
        if (digit_in_flight(update.at(get_update_bit(update_definitions::Flags::APP, true))))
        {
            update.at(get_update_bit(update_definitions::Flags::APP, true)) =
                digit_settled(update.at(get_update_bit(update_definitions::Flags::APP, true)));
        }
        write_update_bits(*this->uboot_handler, update);
        /* The pending-state boots of the rollback cycle drained the running
         * slot's boot budget (mark-good is gated while a state is pending);
         * without a restore the next reboot silently selects the other slot. */
        this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
        this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
        this->uboot_handler->addVariable(
            "update_reboot_state",
            update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
    }
    else
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                                                   std::string("Stop rollback commit. Wrong update reboot state."),
                                                   logger::logLevel::ERROR));
        throw(ConfirmPendingRollback());
    }
    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Finish rollback commit"), logger::logLevel::DEBUG));
}

bool updater::Bootstate::noUpdateProcessing()
{
    bool retValue = false;
    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);

    if (update_reboot_state == update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
    {
        retValue = true;
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("noUpdateProcessing: no update in process? ") + std::to_string(retValue),
        logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::firmware_update_reboot_failed(const std::string &current_slot,
                                                       const std::string &boot_order_old, const std::string &boot_order,
                                                       const uint8_t &number_of_tries_a,
                                                       const uint8_t &number_of_tries_b)
{
    const bool ret_Value = (((current_slot == util::split(boot_order_old, ' ').front()) &&
                             ((number_of_tries_a == 0) || (number_of_tries_b == 0))) &&
                            (boot_order_old != boot_order));
    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("firmware_update_reboot_failed: ") + std::to_string(ret_Value),
                         logger::logLevel::DEBUG));
    return ret_Value;
}

bool updater::Bootstate::firmware_update_reboot_successful(const std::string &current_slot,
                                                           const std::string &boot_order_old,
                                                           const std::string &boot_order)
{
    const bool ret_Value = ((current_slot == util::split(boot_order, ' ').front()) && (boot_order_old != boot_order));
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("firmware_update_reboot_successful: ") + std::to_string(ret_Value),
        logger::logLevel::DEBUG));
    return ret_Value;
}

bool updater::Bootstate::missing_firmware_update_reboot(const std::string &current_slot,
                                                        const std::string &boot_order_old,
                                                        const std::string &boot_order, const uint8_t &number_of_tries_a,
                                                        const uint8_t &number_of_tries_b)
{
    const bool ret_Value = ((current_slot != util::split(boot_order, ' ').front()) && (number_of_tries_a == 3) &&
                            (number_of_tries_b == 3) && (boot_order_old != boot_order));
    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("missing_firmware_update_reboot: ") + std::to_string(ret_Value),
                         logger::logLevel::DEBUG));
    return ret_Value;
}

bool updater::Bootstate::stalled_install_pending()
{
    if (!this->pendingFirmwareUpdate())
    {
        return false;
    }

    const std::string boot_order_old =
        this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    return this->install_never_activated(boot_order_old, boot_order);
}

int32_t updater::Bootstate::uncommitted_fw_index(const std::vector<uint8_t> &update_bits)
{
    int32_t index = -1;

    for (const int32_t candidate : {FIRMWARE_A_INDEX, FIRMWARE_B_INDEX})
    {
        if ((update_bits.size() > static_cast<size_t>(candidate)) && digit_in_flight(update_bits.at(candidate)))
        {
            /* Two slots in flight at once is not a state this repair can name. */
            if (index >= 0)
            {
                return -1;
            }
            index = candidate;
        }
    }

    return index;
}

bool updater::Bootstate::install_never_activated(const std::string &boot_order_old, const std::string &boot_order)
{
    const std::vector<uint8_t> update_bits =
        util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
    const bool ret_Value = ((boot_order_old == boot_order) && (this->uncommitted_fw_index(update_bits) >= 0));
    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("install_never_activated: ") + std::to_string(ret_Value),
                         logger::logLevel::DEBUG));
    return ret_Value;
}

updater::Bootstate::AppImageState updater::Bootstate::application_reboot(const std::string &sysfs_block_root)
{
    fs::util::DirGuard block_dir(::opendir(sysfs_block_root.c_str()));
    if (!block_dir.valid())
    {
        const std::string error_msg = "cannot open " + sysfs_block_root;
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("application_reboot: ") + error_msg, logger::logLevel::ERROR));
        throw(GetLoopDevices(error_msg));
    }

    /* One naming authority for the application image: the configured store
     * decides the directory and the file name together. Matching the bare name
     * anywhere would accept any copy as the running application -- a leftover in a
     * temporary directory, a stale mount from an older store, a second store on
     * the same device. */
    const std::string expected_image = fs::app_slot_image_path(
        updater::config::STANDARD_APP_IMG_STORE,
        this->uboot_handler->getVariable("application", allowed_application_variables));

    bool application_reboot = false;
    bool any_loop_device_readable = false;
    struct dirent *entry = nullptr;
    while (((entry = ::readdir(block_dir.get())) != nullptr) && (application_reboot == false))
    {
        const std::string name(entry->d_name);
        if (name.rfind("loop", 0) != 0)
        {
            continue;
        }

        const std::string backing_file_path =
            fs::util::path_join(sysfs_block_root, name + "/loop/backing_file");
        std::ifstream backing_file(backing_file_path, std::ifstream::in);
        if (!backing_file.good())
        {
            continue;
        }
        any_loop_device_readable = true;

        std::string output;
        std::getline(backing_file, output);
        application_reboot = (output == expected_image);
    }

    /* Expected pre-mount: nothing loop-mounted yet, so the reboot question
     * has no answer here - report that instead of failing. */
    if (!any_loop_device_readable)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("application_reboot: no loop*/loop/backing_file readable under ") + sysfs_block_root,
            logger::logLevel::DEBUG));
        return AppImageState::NOT_MOUNTED;
    }

    if (application_reboot == false)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("application_reboot: no loop device backing file matched the expected application slot"),
            logger::logLevel::DEBUG));
        return AppImageState::OTHER_SLOT_MOUNTED;
    }

    return AppImageState::ACTIVE_SLOT_MOUNTED;
}

void updater::Bootstate::firmware_rollback()
{
    /* The recovery marker outranks every rollback: overwriting it would
     * destroy the only evidence of a state this build cannot decode. Refuse
     * before anything is staged; a partial stage is worse than none. */
    if (update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger) ==
        update_definitions::UBootBootstateFlags::UNKNOWN_STATE)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("firmware_rollback: update_reboot_state not interpretable, refusing rollback"),
            logger::logLevel::ERROR));
        throw(RebootStateNotInterpretable());
    }

    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();

    /* check for missing reboot after update */
    if (this->missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                             number_of_tries_b) == true)
    {
        /* The reboot never happened, so the slot being abandoned is the one the
         * install wrote -- the NEXT one. Settling the running slot instead would
         * leave the abandoned slot recorded uncommitted while the machine reports
         * idle, and a later slot switch refused with no way to explain why. A
         * voluntary rollback settles the abandoned slot; only a demonstrated boot failure records
         * it bad. Settling clears the uncommitted bit alone, so a slot that
         * carried no verdict becomes committed and one that did keeps it: a
         * mark already standing is not disproved by abandoning an update. */
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        const int32_t abandoned_fw = get_update_bit(update_definitions::Flags::OS, true);
        update.at(abandoned_fw) = digit_settled(update.at(abandoned_fw));
        write_update_bits(*this->uboot_handler, update);
        this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
        this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
        this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
        this->uboot_handler->addVariable(
            "update_reboot_state",
            update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("firmware_rollback: missing_firmware_update_reboot state, reset to old bootstate successful"),
            logger::logLevel::DEBUG));
    }
    /* check for reboot after update  */
    else if (this->firmware_update_reboot_successful(current_slot, boot_order_old, boot_order) == true)
    {
        if (current_slot == "A")
        {
            this->uboot_handler->addVariable("BOOT_A_LEFT", "0");
        }
        else
        {
            this->uboot_handler->addVariable("BOOT_B_LEFT", "0");
        }
        this->uboot_handler->addVariable(
            "update_reboot_state",
            update_definitions::to_string(update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING));
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string(
                "firmware_rollback: firmware_update_reboot_successful state, reset to old bootstate successful"),
            logger::logLevel::DEBUG));
    }
    /* check for reboot after success fail */
    else if (this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                                 number_of_tries_b) == true)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("firmware_rollback: Failed update reboot, a rollback is done"),
            logger::logLevel::WARNING));
    }
}

void updater::Bootstate::applicaton_rollback(const std::function<void()> &app_rollback,
                                             const std::string &sysfs_block_root)
{
    /* The recovery marker outranks every rollback: overwriting it would
     * destroy the only evidence of a state this build cannot decode. Refuse
     * before anything is staged; a partial stage is worse than none. */
    if (update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger) ==
        update_definitions::UBootBootstateFlags::UNKNOWN_STATE)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("applicaton_rollback: update_reboot_state not interpretable, refusing rollback"),
            logger::logLevel::ERROR));
        throw(RebootStateNotInterpretable());
    }

    const AppImageState app_image_state = this->application_reboot(sysfs_block_root);
    if (app_image_state == AppImageState::ACTIVE_SLOT_MOUNTED)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("applicaton_rollback: uncommited application -> reboot mandatory"),
            logger::logLevel::DEBUG));
        app_rollback();
        this->uboot_handler->addVariable(
            "update_reboot_state",
            update_definitions::to_string(update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING));
    }
    else
    {
        /* NOT_MOUNTED settles the same way: pre-mount (e.g. bootguard's
         * trial-exhaustion revert) the new image never took effect, so no
         * reboot is needed - and throwing here would loop that revert
         * forever. Distinct log line only, for field diagnosis. */
        if (app_image_state == AppImageState::NOT_MOUNTED)
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("applicaton_rollback: no app image mounted -> no reboot mandatory"),
                logger::logLevel::DEBUG));
        }
        else
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("applicaton_rollback: uncommited application -> no reboot mandatory"),
                logger::logLevel::DEBUG));
        }
        app_rollback();
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        /* Only the in-flight bit: the boot guard marks the running slot bad and
         * issues this rollback in the same boot, so the digit can arrive here
         * carrying the quarantine it just recorded. */
        update.at(get_update_bit(update_definitions::Flags::APP, false)) =
            digit_settled(update.at(get_update_bit(update_definitions::Flags::APP, false)));
        write_update_bits(*this->uboot_handler, update);
        this->uboot_handler->addVariable(
            "update_reboot_state",
            update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
    }
}

updater::Bootstate::AppRollbackOutcome
updater::Bootstate::classify_app_rollback(const std::string &sysfs_block_root)
{
    /* The bitfield answers first, and without touching the loop devices: a
     * running application slot still marked uncommitted is the rollback's own
     * durable record that it was enacted, and that record survives a boot in
     * which nothing could be mounted. Probing sysfs first would make this
     * shape depend on evidence it does not need. */
    const std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);
    if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) ==
         update_state.end()) &&
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) !=
         update_state.end()))
    {
        return AppRollbackOutcome::COMMIT_REQUESTED;
    }

    switch (this->application_reboot(sysfs_block_root))
    {
    case AppImageState::ACTIVE_SLOT_MOUNTED:
        /* The revert put the selector back and this boot mounted what it
         * selects: the reboot landed, only the bookkeeping is left. */
        return AppRollbackOutcome::COMMIT_REQUESTED;

    case AppImageState::NOT_MOUNTED:
        /* Genuinely unanswerable: whether the reboot happened cannot be told
         * without a mount, and that is what the caller reports. It is not a
         * refusal. The revert was decided and enacted before this state was
         * written -- the slot switch has happened and no boot changes it back
         * -- so nothing is left to validate and the commit is the way out.
         * Refusing here would leave every verb refusing and the device
         * parked; a re-drive reaches the same unmountable image every
         * time. The rollback actor one level down settles this shape for
         * the same reason. */
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            "classify_app_rollback: no app image mounted; the commit settles this on the "
            "switch that already happened",
            logger::logLevel::WARNING));
        return AppRollbackOutcome::INDETERMINATE;

    case AppImageState::OTHER_SLOT_MOUNTED:
    default:
        /* The image the selector no longer names is still mounted, so the
         * reboot into the rolled-back slot is still owed. */
        return AppRollbackOutcome::REBOOT_OUTSTANDING;
    }
}

updater::Bootstate::FwRebootOutcome updater::Bootstate::classify_fw_reboot()
{
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);
    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);
    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();

    FwRebootOutcome outcome = FwRebootOutcome::PENDING;
    if (current_slot == util::split(boot_order, ' ').front())
    {
        outcome = FwRebootOutcome::BOOTED_PREFERRED;
    }
    else if ((current_slot == util::split(boot_order_old, ' ').front()) &&
             ((number_of_tries_a == 0) || (number_of_tries_b == 0)))
    {
        outcome = FwRebootOutcome::REVERTED;
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN,
        std::string("classify_fw_reboot: ") + std::to_string(static_cast<unsigned>(outcome)),
        logger::logLevel::DEBUG));
    return outcome;
}

bool updater::Bootstate::firmware_reboot()
{
    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();

    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::read_update_reboot_state(*this->uboot_handler, this->logger);
    /* A reboot before apply already moved the slot; state must be re-derived. */
    if ((update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING) ||
        (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING))
    {
        if (current_slot != util::split(boot_order, ' ').front())
        {
            return false;
        }
        return true;
    }

    return !missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                           number_of_tries_b);
}

int32_t updater::Bootstate::get_update_bit(update_definitions::Flags flag, bool next)
{
    int32_t updatebit_index = FIRMWARE_A_INDEX;
    /**
     * 4 states to handle
     * | current slot/app  |   next   |  update bit
     * -----------------------------------------------
     * |      A            |   true   |  (FIRMWARE/APPLICATION)_B_INDEX
     * |      A            |   false  |  (FIRMWARE/APPLICATION)_A_INDEX
     * |      B            |   true   |  (FIRMWARE/APPLICATION)_A_INDEX
     * |      B            |   false  |  (FIRMWARE/APPLICATION)_B_INDEX
     */

    if (flag == update_definitions::Flags::OS)
    {
        const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
        const std::string current_slot = util::split(rauc_cmd, '=').back();
        if ((current_slot == "B") && (next == false))
        {
            updatebit_index = FIRMWARE_B_INDEX;
        }
        else
        {
            if ((current_slot == "A") && (next == true))
            {
                updatebit_index = FIRMWARE_B_INDEX;
            }
        }
    }
    else
    {
        const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
        updatebit_index = APPLICATION_A_INDEX;
        if ((current_app == 'B') && (next == false))
        {
            updatebit_index = APPLICATION_B_INDEX;
        }
        else
        {
            if ((current_app == 'A') && (next == true))
            {
                updatebit_index = APPLICATION_B_INDEX;
            }
        }
    }

    return updatebit_index;
}
