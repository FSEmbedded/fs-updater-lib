#include "handleUpdate.h"

#include "../uboot_interface/allowed_uboot_variable_states.h"
#include "utils.h"
#include <algorithm>
#include <utility>
#include <cerrno>
#include <fstream>

updater::Bootstate::Bootstate(const std::shared_ptr<UBoot::UBoot> &ptr,
                              const std::shared_ptr<logger::LoggerHandler> &logger, std::string loop_backing_file,
                              std::string boot_id_file)
    : uboot_handler(ptr),
      logger(logger),
      loop_backing_file(std::move(loop_backing_file)),
      boot_id_file(std::move(boot_id_file))
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
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
            this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
        {
            retValue = true;
        }
    }

    /* An install that was stopped while writing sets the uncommitted bit on the
     * slot it was writing to and never switches the running one, so the check
     * above looks at the wrong slot and no command can leave the state.
     * Same shape as the firmware path below.
     */
    if (!retValue)
    {
        std::vector<update_definitions::Flags> next_state = this->get_complete_update(true);

        if ((std::find(next_state.begin(), next_state.end(), update_definitions::Flags::OS) == next_state.end()) &&
            (std::find(next_state.begin(), next_state.end(), update_definitions::Flags::APP) != next_state.end()))
        {
            const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
                this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

            if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
            {
                retValue = true;
            }
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
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
            this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

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
            const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
                this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

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
    std::vector<update_definitions::Flags> next_state = this->get_complete_update(true);

    /* Either firmware slot with either application slot. After a failed reboot
     * U-Boot falls back to the old slot, so the firmware bit sits on the next
     * slot; an install stopped before it named the written application slot
     * leaves that bit on the next application slot.
     */
    const auto has = [](const std::vector<update_definitions::Flags> &state, update_definitions::Flags flag) {
        return std::find(state.begin(), state.end(), flag) != state.end();
    };
    const bool os = has(update_state, update_definitions::Flags::OS) || has(next_state, update_definitions::Flags::OS);
    const bool app =
        has(update_state, update_definitions::Flags::APP) || has(next_state, update_definitions::Flags::APP);

    if (os && app)
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
            this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        retValue = (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE);
    }

    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                         std::string("pendingApplicationFirmwareUpdate: is a firmware & application update pending? ") +
                             std::to_string(retValue),
                         logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::app_install_unnamed()
{
    const auto uncommitted = [this](bool next) {
        const auto state = this->get_complete_update(next);
        return std::find(state.begin(), state.end(), update_definitions::Flags::APP) != state.end();
    };
    return !uncommitted(false) && uncommitted(true);
}

bool updater::Bootstate::failedFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(true);

    if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) &&
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
            this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

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
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
            this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

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
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
            this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

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
        /* current_slot==front(BOOT_ORDER) is ambiguous by itself: a pending
         * update's own rollback trigger (firmware_rollback()'s BOOT_x_LEFT=0
         * branch) reads this exact condition true immediately, before the
         * reboot it forces has happened, because it never touches
         * BOOT_ORDER. A switch_firmware_slot between two committed slots
         * reads it true only afterwards, because it swaps BOOT_ORDER itself
         * at trigger time and touches no counter. The drained counter tells
         * them apart in the normal case: the forced-reboot trigger always
         * leaves the about-to-run slot at zero, a plain switch never sets
         * it. This is not exhaustive: if the switched-to slot's counter was
         * already at 1 (natural decrement, or a prior drain from unrelated
         * boots), its one real reboot also lands on zero and this still
         * misreads it as the pre-reboot window. The stamp answers the
         * question directly; the counter decides only without one.
         */
        const unsigned int current_slot_left = (current_slot == "A") ? number_of_tries_a : number_of_tries_b;
        const RebootSinceStateWrite rebooted = this->rebooted_since_state_write();
        if (rebooted == RebootSinceStateWrite::NO ||
            (rebooted == RebootSinceStateWrite::UNKNOWN && current_slot_left == 0))
        {
            /* Reboot after rollback required */
            return false;
        }
        return true;
    }
    /* check reboot state after update fails rollback pending */
    /* true - means rollback pending and false is not */
    return firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                         number_of_tries_b);
}

bool updater::Bootstate::pendingUpdateRollback(update_definitions::UBootBootstateFlags &update_reboot_state)
{
    /* Check for incomplete state */
    if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK)
    {
        this->logger->setLogEntry(
            std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Common update rollback pending"), logger::logLevel::DEBUG));
        return true;
    }
    else if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK)
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
    if (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING ||
        update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING)
    {
        /* The OS digit alone cannot decide this: firmware_rollback()'s
         * pending-update trigger (BOOT_<slot>_LEFT=0) never touches
         * "update", so the digit set at install time stays present for as
         * long as the rollback is unconfirmed - through the window before
         * its own forced reboot, not just after it. Checking the digit
         * first let a commit right after --rollback_update
         * succeed and silently keep that digit, because it never asked
         * whether the reboot the rollback itself demands had happened.
         * pendingFirmwareRollback() answers that question directly.
         */
        return this->pendingFirmwareRollback();
    }
    else if (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING)
    {
        bool pending = false;
        if ((std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) ==
             update_state.end()) &&
            (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()))
        {
            pending = true;
        }
        else if (this->application_reboot() == true)
        {
            /* After rollback env. application was changed to old state.
             * That means that mounted application before reboot is not same to env. state.
             * and reboot required. Otherwise rollback pending.
             */
            pending = true;
        }

        return pending;
    }

    return false;
}

bool updater::Bootstate::rollbackInProgress(const update_definitions::UBootBootstateFlags &update_reboot_state)
{
    const bool retValue =
        update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING ||
        update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING ||
        update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING ||
        update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK ||
        update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_ROLLBACK ||
        update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK;

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("rollbackInProgress: a rollback is already under way? ") + std::to_string(retValue),
        logger::logLevel::DEBUG));
    return retValue;
}

void updater::Bootstate::confirmFailedFirmwareUpdate()
{
    if (this->failedFirmwareUpdate() == true)
    {
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        update.at(get_update_bit(update_definitions::Flags::OS, true)) = '2';

        const update_definitions::UBootBootstateFlags update_reboot_state =
            update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->stage_update_reboot_state(update_reboot_state);

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
    if (this->failedRebootFirmwareUpdate() == true)
    {
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        update.at(get_update_bit(update_definitions::Flags::OS, false)) = '2';
        const update_definitions::UBootBootstateFlags update_reboot_state =
            update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->stage_update_reboot_state(update_reboot_state);

        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("confirmFailedRebootFirmwareUpdate: failed update reboot firmware update is confirmed"),
            logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("confirmFailedRebootFirmwareUpdate: no failed update reboot firmware update to confirm"),
            logger::logLevel::ERROR));
        throw(ConfirmFailedRebootFirmwareUpdate("no failed reboot firmware update detected"));
    }
}

void updater::Bootstate::confirmFailedApplicationeUpdate()
{
    if (this->failedApplicationUpdate() == true)
    {
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));

        update.at(get_update_bit(update_definitions::Flags::APP, true)) = '2';
        const update_definitions::UBootBootstateFlags update_reboot_state =
            update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->stage_update_reboot_state(update_reboot_state);

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

        /* A drained counter reads as a failed reboot even when none happened. */
        if (this->rebooted_since_state_write() == RebootSinceStateWrite::NO)
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: no reboot since the install"),
                logger::logLevel::ERROR));
            throw(MissingReboot("firmware update requires reboot before commit"));
        }

        const FirmwareOutcome outcome = this->pending_firmware_outcome();
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        if (outcome == FirmwareOutcome::TARGET_INCOMPLETE || outcome == FirmwareOutcome::TARGET_DID_NOT_BOOT ||
            this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                                number_of_tries_b))
        {
            this->logger->setLogEntry(
                std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: the installed firmware never ran, marking slot as bad"),
                                 logger::logLevel::ERROR));
            this->record_failed_firmware_target(update);
        }
        else if (outcome == FirmwareOutcome::TARGET_UNTOUCHED)
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN,
                std::string("confirmPendingFirmwareUpdate: the install stopped before it wrote the slot, clearing it"),
                logger::logLevel::ERROR));
            update.at(get_update_bit(update_definitions::Flags::OS, true)) = '0';
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("BOOT_" + current_slot + "_LEFT", "3");
            this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
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

            update.at(get_update_bit(update_definitions::Flags::OS, false)) = '0';
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
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

void updater::Bootstate::confirmPendingApplicationUpdate()
{
    if (this->pendingApplicationUpdate())
    {
        const bool application_reboot = this->application_reboot();
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));

        if (application_reboot)
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, "confirmPendingApplicationUpdate: mark application update as successful",
                logger::logLevel::DEBUG));
            /* The pending update sits on the running slot once it has been
             * started, and on the other one while it never got that far.
             * Clear the bit that is actually set, or the slot stays flagged
             * for good and later refuses a rollback.
             */
            int32_t update_bit = get_update_bit(update_definitions::Flags::APP, false);
            if (((update.at(update_bit) - '0') & STATE_UPDATE_UNCOMMITED) != STATE_UPDATE_UNCOMMITED)
            {
                update_bit = get_update_bit(update_definitions::Flags::APP, true);
            }
            update.at(update_bit) = '0';
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
        }
        else
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, "confirmPendingApplicationUpdate: missing reboot for application update",
                logger::logLevel::ERROR));
            throw(MissingReboot("application update requires reboot before commit"));
        }
    }
}

void updater::Bootstate::confirmPendingApplicationFirmwareUpdate()
{
    if (this->pendingApplicationFirmwareUpdate())
    {
        /* "application" names the written slot only when the install got that
         * far; otherwise the digit sits on the other slot and there is no
         * flip to undo.
         */
        const bool app_unnamed = this->app_install_unnamed();
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

        /* A drained counter reads as a failed reboot even when none happened. */
        if (this->rebooted_since_state_write() == RebootSinceStateWrite::NO)
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: no reboot since the install"),
                logger::logLevel::ERROR));
            throw(MissingReboot("firmware & application update requires reboot before commit"));
        }

        /* The combined state is staged only once the firmware install has
         * returned, so its target was written: either it runs or it did not
         * boot. */
        const FirmwareOutcome outcome = this->pending_firmware_outcome();
        if (outcome == FirmwareOutcome::TARGET_INCOMPLETE || outcome == FirmwareOutcome::TARGET_DID_NOT_BOOT ||
            this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                                number_of_tries_b))
        {
            const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
            std::vector<uint8_t> update =
                util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
            update.at(get_update_bit(update_definitions::Flags::APP, app_unnamed)) = '0';

            if (app_unnamed)
            {
                this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                    BOOTSTATE_DOMAIN,
                    "confirmApplicationFirmwareUpdate: application install never named the written slot, no flip",
                    logger::logLevel::DEBUG));
            }
            else if (current_app == 'A')
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
                BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: the installed firmware never ran, marking slot as bad"),
                logger::logLevel::ERROR));

            this->record_failed_firmware_target(update);
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

            update.at(get_update_bit(update_definitions::Flags::OS, false)) = '0';
            update.at(get_update_bit(update_definitions::Flags::APP, app_unnamed)) = '0';

            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
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

bool updater::Bootstate::commit_running_firmware_slot(std::vector<uint8_t> &update)
{
    /* The slot the board runs is the one to keep: the digit of the slot left
     * behind cannot tell a switch that booted from one U-Boot skipped.
     */
    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();
    const std::string other_slot = (current_slot == "A") ? "B" : "A";
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);
    uint8_t &other_digit = update.at(get_update_bit(update_definitions::Flags::OS, true));

    const bool not_taken = other_digit == '0' && util::split(boot_order, ' ').front() != current_slot;
    if (not_taken)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("Switch to slot ") + other_slot + " did not hold: U-Boot runs slot " + current_slot +
                ", which stays; slot " + other_slot + " is still marked good.",
            logger::logLevel::WARNING));
    }
    if (other_digit != '0')
    {
        other_digit = '2';
    }
    this->uboot_handler->addVariable("BOOT_ORDER", current_slot + " " + other_slot);
    this->uboot_handler->addVariable("BOOT_ORDER_OLD", current_slot + " " + other_slot);
    return not_taken;
}

void updater::Bootstate::record_failed_firmware_target(std::vector<uint8_t> &update)
{
    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();
    /* BOOT_ORDER is whatever RAUC left, a single slot included; the target's
     * attempts stay drained so the bootloader keeps skipping it. */
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    update.at(get_update_bit(update_definitions::Flags::OS, true)) = '2';
    this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
    this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
    this->uboot_handler->addVariable("BOOT_" + current_slot + "_LEFT", "3");
    this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
}

updater::Bootstate::FirmwareOutcome updater::Bootstate::pending_firmware_outcome()
{
    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
        this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
    FirmwareOutcome outcome = FirmwareOutcome::UNDECIDED;
    std::string reason;

    /* Only a proven reboot tells "never ran" from "not yet". */
    if ((update_reboot_state != update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE &&
         update_reboot_state != update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE) ||
        this->rebooted_since_state_write() != RebootSinceStateWrite::YES)
    {
        reason = "no pending firmware update with a proven reboot";
    }
    else
    {
        const std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        const std::string current_slot =
            util::split(this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables), '=').back();
        std::string target;
        for (const auto &slot : {std::make_pair(FIRMWARE_A_INDEX, "A"), std::make_pair(FIRMWARE_B_INDEX, "B")})
        {
            if (((update.at(slot.first) - '0') & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
            {
                target = slot.second;
            }
        }
        const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);
        const std::vector<std::string> order = util::split(boot_order, ' ');

        if (target.empty())
        {
            reason = "no uncommitted firmware digit";
        }
        else if (target == current_slot)
        {
            outcome = FirmwareOutcome::BOOTED;
            reason = "slot " + target + " runs";
        }
        else if (std::find(order.begin(), order.end(), target) == order.end())
        {
            outcome = FirmwareOutcome::TARGET_INCOMPLETE;
            reason = "slot " + target + " is shut out of BOOT_ORDER \"" + boot_order + "\"";
        }
        else if (this->uboot_handler->getVariable("BOOT_" + target + "_LEFT", allowed_boot_ab_left_variables) == 0)
        {
            outcome = FirmwareOutcome::TARGET_DID_NOT_BOOT;
            reason = "slot " + target + " has no attempts left";
        }
        else if (this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables) == boot_order)
        {
            outcome = FirmwareOutcome::TARGET_UNTOUCHED;
            reason = "BOOT_ORDER \"" + boot_order + "\" is still the order from before the install";
        }
        else
        {
            reason = "slot " + target + " is bootable in BOOT_ORDER \"" + boot_order + "\" but slot " + current_slot +
                     " runs";
        }
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("pending_firmware_outcome: ") + std::to_string(static_cast<int>(outcome)) + " (" + reason + ")",
        logger::logLevel::DEBUG));
    return outcome;
}

update_definitions::UBootBootstateFlags updater::Bootstate::reported_update_reboot_state()
{
    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
        this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
    if (update_reboot_state != update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE &&
        update_reboot_state != update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
    {
        return update_reboot_state;
    }

    switch (this->pending_firmware_outcome())
    {
    case FirmwareOutcome::TARGET_INCOMPLETE:
    case FirmwareOutcome::TARGET_UNTOUCHED:
        return update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE;
    case FirmwareOutcome::TARGET_DID_NOT_BOOT:
        return update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED;
    default:
        return update_reboot_state;
    }
}

void updater::Bootstate::mark_unbooted_firmware_target_bad(const std::function<void()> &mark_other_bad)
{
    FirmwareOutcome outcome;
    {
        UBoot::UBoot::EnvTransaction txn(*this->uboot_handler);
        outcome = this->pending_firmware_outcome();
    }
    if (outcome == FirmwareOutcome::TARGET_INCOMPLETE || outcome == FirmwareOutcome::TARGET_DID_NOT_BOOT)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("mark_unbooted_firmware_target_bad: the installed firmware never ran, RAUC marks the slot bad"),
            logger::logLevel::WARNING));
        mark_other_bad();
    }
}

void updater::Bootstate::stage_boot_order_before_install()
{
    const std::string current_slot =
        util::split(this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables), '=').back();
    const std::string other_slot = (current_slot == "A") ? "B" : "A";
    this->uboot_handler->addVariable("BOOT_ORDER_OLD", current_slot + " " + other_slot);
}

bool updater::Bootstate::confirmUpdateRollback()
{
    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Start rollback commit"), logger::logLevel::DEBUG));
    bool switch_not_taken = false;
    /* Check for the last update reboot state */
    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
        this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
    /* --apply_update writes its state before the reboot it starts; a commit in
     * between would cancel the rollback.
     */
    if (this->rebooted_since_state_write() == RebootSinceStateWrite::NO)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("Stop rollback commit. No reboot since the rollback."), logger::logLevel::ERROR));
        throw(MissingReboot("rollback requires reboot before commit"));
    }
    if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK ||
        update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("Commit firmware and application rollback."), logger::logLevel::DEBUG));
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        switch_not_taken = this->commit_running_firmware_slot(update);
        /* Mark uncommitted application slot as bad */
        if (update.at(get_update_bit(update_definitions::Flags::APP, true)) == '1')
        {
            update.at(get_update_bit(update_definitions::Flags::APP, true)) = '2';
        }
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
        this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
        this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
    }
    else if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK ||
             update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Firmware update rollback pending"),
                                                   logger::logLevel::DEBUG));
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        switch_not_taken = this->commit_running_firmware_slot(update);
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
        this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
        this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
    }
    else if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_ROLLBACK ||
             update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("Application update rollback pending"),
                                                   logger::logLevel::DEBUG));
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        /* Mark uncommitted application slot as bad */
        if (update.at(get_update_bit(update_definitions::Flags::APP, true)) == '1')
        {
            update.at(get_update_bit(update_definitions::Flags::APP, true)) = '2';
        }
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
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
    return switch_not_taken;
}

bool updater::Bootstate::noUpdateProcessing()
{
    bool retValue = false;
    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
        this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

    if (update_reboot_state == update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
    {
        retValue = true;
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("noUpdateProcessing: no update in process? ") + std::to_string(retValue),
        logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::updateDigitsAllCommitted()
{
    const std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
    bool retValue = true;

    for (const uint8_t digit : update)
    {
        if (((digit - '0') & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
        {
            retValue = false;
            break;
        }
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("updateDigitsAllCommitted: all four digits committed? ") + std::to_string(retValue),
        logger::logLevel::DEBUG));
    return retValue;
}

std::string updater::Bootstate::uncommittedDigitsHint()
{
    const std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
    const std::string running_fw = util::split(this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables), '=').back();
    const char running_app = this->uboot_handler->getVariable("application", allowed_application_variables);

    const struct
    {
        int index;
        bool firmware;
        char slot;
    } slots[] = {{FIRMWARE_A_INDEX, true, 'A'},
                 {APPLICATION_A_INDEX, false, 'A'},
                 {FIRMWARE_B_INDEX, true, 'B'},
                 {APPLICATION_B_INDEX, false, 'B'}};

    std::string hint;
    for (const auto &s : slots)
    {
        const int state = update.at(s.index) - '0';
        if ((state & STATE_UPDATE_UNCOMMITED) != STATE_UPDATE_UNCOMMITED)
        {
            continue;
        }

        const std::string slot(1, s.slot);
        const std::string kind = s.firmware ? "firmware" : "application";
        const bool running = s.firmware ? (running_fw == slot) : (running_app == s.slot);

        hint += hint.empty() ? "" : "; ";
        if ((state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD)
        {
            hint += kind + " slot " + slot +
                    " is marked bad and uncommitted (3); no fs-updater command changes this (--set_*_state_bad "
                    "reports success and leaves it as is), the update environment has to be repaired outside "
                    "fs-updater";
            continue;
        }

        hint += kind + " slot " + slot + (running ? " (running)" : " (not running)") +
                " is still marked uncommitted by an earlier update; clear it with --set_" +
                (s.firmware ? "fw" : "app") + "_state_bad " + slot + ". ";
        hint += (s.firmware && running) ? "Slot " + slot + " keeps booting, but is no longer accepted as a rollback target until an update installs it again"
                                        : "Slot " + slot + " is then no longer accepted as a rollback target until an update installs it again";
    }

    if (hint.empty())
    {
        hint = "an earlier update left an uncommitted digit";
    }
    return hint + " - a new update is refused until this is resolved";
}

/* A single-slot boot_order ("A" or "B") is RAUC's bad marker for the other slot:
 * an install shuts its target out before writing it and only a completed install
 * puts it back. Within the install's own boot that shape is also what a write in
 * progress looks like; boot_order_old != boot_order then holds without any update
 * flipped, so without the two-field guard firmware_update_reboot_failed and
 * firmware_update_reboot_successful would read a mid-write as failed/successful and
 * rollback_firmware() would zero the only slot left; missing_firmware_update_reboot
 * gets the same guard for consistency. After a reboot pending_firmware_outcome
 * reads the marker for what it is. Checking for two split fields is safe only
 * because allowed_boot_order_variables never admits a value, such as a trailing
 * space, that would also split to two fields without naming two slots.
 */
bool updater::Bootstate::firmware_update_reboot_failed(const std::string &current_slot,
                                                       const std::string &boot_order_old, const std::string &boot_order,
                                                       const uint8_t &number_of_tries_a,
                                                       const uint8_t &number_of_tries_b)
{
    const bool ret_Value = (((current_slot == util::split(boot_order_old, ' ').front()) &&
                             ((number_of_tries_a == 0) || (number_of_tries_b == 0))) &&
                            (boot_order_old != boot_order) && (util::split(boot_order, ' ').size() == 2));
    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("firmware_update_reboot_failed: ") + std::to_string(ret_Value),
                         logger::logLevel::DEBUG));
    return ret_Value;
}

bool updater::Bootstate::firmware_update_reboot_successful(const std::string &current_slot,
                                                           const std::string &boot_order_old,
                                                           const std::string &boot_order)
{
    const bool ret_Value = ((current_slot == util::split(boot_order, ' ').front()) && (boot_order_old != boot_order) &&
                            (util::split(boot_order, ' ').size() == 2));
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
                            (number_of_tries_b == 3) && (boot_order_old != boot_order) &&
                            (util::split(boot_order, ' ').size() == 2));
    this->logger->setLogEntry(
        std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN, std::string("missing_firmware_update_reboot: ") + std::to_string(ret_Value),
                         logger::logLevel::DEBUG));
    return ret_Value;
}

bool updater::Bootstate::install_pending()
{
    const int error = util::stat_error(fs::UPDATE_INSTALLED_MARKER_PATH);
    /* Only a missing file means the reboot happened; a marker that cannot
     * be read counts as present, the refusing side.
     */
    if (error != 0 && error != ENOENT) {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(BOOTSTATE_DOMAIN,
                                                                     std::string("install_pending: cannot read ") +
                                                                         fs::UPDATE_INSTALLED_MARKER_PATH + ": error " +
                                                                         std::to_string(error) + ", treated as present",
                                                                     logger::logLevel::ERROR));
    }
    const bool pending = (error != ENOENT);
    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN, std::string("install_pending: ") + (pending ? "1" : "0"), logger::logLevel::DEBUG));
    return pending;
}

std::string updater::Bootstate::read_boot_id()
{
    std::ifstream file(this->boot_id_file);
    std::string id;
    std::getline(file, id);
    if (id.empty())
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("read_boot_id: cannot read ") + this->boot_id_file, logger::logLevel::ERROR));
    }
    return id;
}

void updater::Bootstate::stage_update_reboot_state(update_definitions::UBootBootstateFlags flag, bool keep_boot_id)
{
    std::string id;
    if (keep_boot_id)
    {
        /* Only a stamp that belongs to the state it replaces carries a boot id
         * worth keeping; anything else is left unusable on purpose.
         */
        const std::string state = update_definitions::to_string(update_definitions::to_UBootBootstateFlags(
            this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables)));
        std::string stamp;
        try
        {
            stamp = this->uboot_handler->getVariable("update_reboot_stamp");
        }
        catch (const UBoot::UBootEnvAccess &)
        {
        }
        if (stamp.compare(0, state.size() + 1, state + ":") == 0)
        {
            id = stamp.substr(state.size() + 1);
        }
    }
    else
    {
        id = this->read_boot_id();
    }

    const std::string value = update_definitions::to_string(flag);
    this->uboot_handler->addVariable("update_reboot_state", value);
    this->uboot_handler->addVariable("update_reboot_stamp", value + ":" + id);
}

updater::Bootstate::RebootSinceStateWrite updater::Bootstate::rebooted_since_state_write()
{
    const std::string state = update_definitions::to_string(update_definitions::to_UBootBootstateFlags(
        this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables)));
    std::string stamp;
    try
    {
        stamp = this->uboot_handler->getVariable("update_reboot_stamp");
    }
    catch (const UBoot::UBootEnvAccess &)
    {
    }

    RebootSinceStateWrite result = RebootSinceStateWrite::UNKNOWN;
    const std::string now = this->read_boot_id();
    if (!now.empty() && stamp.size() > state.size() + 1 && stamp.compare(0, state.size() + 1, state + ":") == 0)
    {
        result = (stamp.substr(state.size() + 1) == now) ? RebootSinceStateWrite::NO : RebootSinceStateWrite::YES;
    }

    this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
        BOOTSTATE_DOMAIN,
        std::string("rebooted_since_state_write: ") +
            (result == RebootSinceStateWrite::YES ? "yes" : result == RebootSinceStateWrite::NO ? "no" : "unknown"),
        logger::logLevel::DEBUG));
    return result;
}

bool updater::Bootstate::application_reboot()
{
    bool application_reboot = false;
    std::ifstream mounted_devices(this->loop_backing_file, std::ifstream::in);
    if (mounted_devices.good())
    {
        do
        {
            std::string output;
            std::getline(mounted_devices, output);
            application_reboot =
                ((output.find("app_a.squashfs") != std::string::npos) &&
                 ('A' == this->uboot_handler->getVariable("application", allowed_application_variables))) ||
                ((output.find("app_b.squashfs") != std::string::npos) &&
                 ('B' == this->uboot_handler->getVariable("application", allowed_application_variables)));
        } while ((mounted_devices.eof() == false) && (application_reboot == false));

        if ((mounted_devices.eof() == true) && (application_reboot == false))
        {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN,
                std::string("application_reboot: No application image in ") + this->loop_backing_file + " mounted",
                logger::logLevel::DEBUG));
        }
    }
    else
    {
        const std::string error_msg = util::describe_stream_error(mounted_devices);
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("application_reboot: ") + error_msg, logger::logLevel::ERROR));
        throw(GetLoopDevices(error_msg));
    }
    return application_reboot;
}

void updater::Bootstate::refuse_rollback_before_reboot()
{
    /* The reboot after an install is mandatory. The marker has to decide
     * before the update digits do: a rollback that reaches them reads the
     * slot the install just wrote as one that needs a commit.
     */
    if (this->install_pending()) {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("refuse_rollback_before_reboot: missing reboot after install"),
            logger::logLevel::ERROR));
        throw(MissingReboot("update requires reboot before rollback"));
    }
    /* A combined install stopped before its marker exists still owes the
     * reboot; the commit refuses it the same way, so the rollback must not
     * ask for a commit first.
     */
    if (this->pendingApplicationFirmwareUpdate() && !this->firmware_reboot()) {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("refuse_rollback_before_reboot: missing reboot after firmware & application update"),
            logger::logLevel::ERROR));
        throw(MissingReboot("firmware & application update requires reboot before rollback"));
    }
}

void updater::Bootstate::firmware_rollback()
{
    /* The combined rollback would flip "application" to a slot that was never
     * verified; the commit clears the digit instead, as for an application-only
     * update.
     */
    if (this->pendingApplicationFirmwareUpdate() && this->app_install_unnamed())
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("firmware_rollback: interrupted application install"),
            logger::logLevel::ERROR));
        throw(CommitRequired("interrupted application update is cleared by the commit"));
    }

    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();

    /* check for missing reboot after update */
    if (this->install_pending() || this->rebooted_since_state_write() == RebootSinceStateWrite::NO ||
        this->missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a,
                                             number_of_tries_b) == true) {
        /* The written slot is left as it is: the rollback that follows the
         * reboot is the only one that marks it.
         */
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("firmware_rollback: missing reboot after firmware update"),
            logger::logLevel::ERROR));
        throw(MissingReboot("firmware update requires reboot before rollback"));
    }

    /* A target that never ran has nothing to roll back to; the commit
     * acknowledges it and the written slot stays bad. */
    const FirmwareOutcome outcome = this->pending_firmware_outcome();
    if (outcome == FirmwareOutcome::TARGET_INCOMPLETE || outcome == FirmwareOutcome::TARGET_DID_NOT_BOOT ||
        outcome == FirmwareOutcome::TARGET_UNTOUCHED)
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("firmware_rollback: the installed firmware never ran, nothing to roll back"),
            logger::logLevel::ERROR));
        throw(CommitRequired("the firmware update failed before its slot ran; the commit acknowledges it"));
    }
    /* check for reboot after update  */
    if (this->firmware_update_reboot_successful(current_slot, boot_order_old, boot_order) == true)
    {
        if (current_slot == "A")
        {
            this->uboot_handler->addVariable("BOOT_A_LEFT", "0");
        }
        else
        {
            this->uboot_handler->addVariable("BOOT_B_LEFT", "0");
        }
        this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING);
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
    else
    {
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("firmware_rollback: firmware update state is illegal"),
            logger::logLevel::ERROR));
        throw(FirmwareRebootStateNotDefined());
    }
}

void updater::Bootstate::applicaton_rollback(updater::updateBase &app_updater)
{

    if (this->application_reboot())
    {
        const std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        /* An install that stopped before it named the written slot leaves
         * the digit on the other one. Flipping to that slot would activate
         * what was never verified; the commit clears the digit instead.
         */
        if (((update.at(get_update_bit(update_definitions::Flags::APP, false)) - '0') & STATE_UPDATE_UNCOMMITED) !=
            STATE_UPDATE_UNCOMMITED) {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("applicaton_rollback: interrupted application install"),
                logger::logLevel::ERROR));
            throw(CommitRequired("interrupted application update is cleared by the commit"));
        }
        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN, std::string("applicaton_rollback: uncommited application -> reboot mandatory"),
            logger::logLevel::DEBUG));
        app_updater.rollback();
        this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING);
    }
    else
    {
        /* The commit asks whether the installed image is mounted; the
         * rollback asks whether a reboot happened at all, because a mount
         * mismatch after a reboot is the one state only an in-place undo
         * can leave.
         */
        if (this->install_pending()) {
            this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
                BOOTSTATE_DOMAIN, std::string("applicaton_rollback: missing reboot after application update"),
                logger::logLevel::ERROR));
            throw(MissingReboot("application update requires reboot before rollback"));
        }

        this->logger->setLogEntry(std::make_shared<logger::LogEntry>(
            BOOTSTATE_DOMAIN,
            std::string("applicaton_rollback: installed application not mounted after reboot -> undo in place"),
            logger::logLevel::WARNING));
        std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        /* Clear the digit that is set, as the commit does. "application"
         * names the written slot only when the install got that far; only
         * then is there a flip to undo. Indices are read before anything
         * is staged.
         */
        int32_t update_bit = get_update_bit(update_definitions::Flags::APP, false);
        if (((update.at(update_bit) - '0') & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED) {
            app_updater.rollback();
        } else {
            update_bit = get_update_bit(update_definitions::Flags::APP, true);
        }
        update.at(update_bit) = '0';
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->stage_update_reboot_state(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING);
    }
}

bool updater::Bootstate::firmware_reboot()
{
    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();

    const RebootSinceStateWrite rebooted = this->rebooted_since_state_write();
    if (rebooted != RebootSinceStateWrite::UNKNOWN)
    {
        return rebooted == RebootSinceStateWrite::YES;
    }

    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(
        this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
    /* In case reboot was executed before apply manually check of system state needed. */
    if ((update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING) ||
        (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING))
    {
        /* A rollback of an unconfirmed install leaves the drained slot in front and
         * its install digit set, a switch between committed slots leaves a clean
         * digit; the running slot leads BOOT_ORDER after the second only.
         */
        const std::vector<std::string> order = util::split(boot_order, ' ');
        if (order.size() != 2)
        {
            return false;
        }
        const std::vector<uint8_t> update =
            util::to_array(this->uboot_handler->getVariable("update", validate_update_bits));
        const bool front_clean =
            update.at(order.front() == "A" ? FIRMWARE_A_INDEX : FIRMWARE_B_INDEX) == '0';
        return front_clean == (current_slot == order.front());
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
