#include "handleUpdate.h"

#include "utils.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"
#include <algorithm>
#include <fstream>

updater::Bootstate::Bootstate(const std::shared_ptr<UBoot::UBoot> & ptr, const std::shared_ptr<logger::LoggerHandler> & logger):
    uboot_handler(ptr),
    logger(logger),
    booted_partition_A("rauc.slot=A"),
    booted_partition_B("rauc.slot=B")
{
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "bootstate: constructor", logger::logLevel::DEBUG));
}

updater::Bootstate::~Bootstate()
{
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "bootstate: deconstruct", logger::logLevel::DEBUG));
}

const std::vector<update_definitions::Flags> updater::Bootstate::get_complete_update(bool next_state)
{
    std::vector<uint8_t> completed_update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    std::vector<update_definitions::Flags> ret_value;

    int current_state = completed_update.at(this->get_update_bit(update_definitions::Flags::OS, next_state)) - '0';

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("get_complete_update: current_state ") + std::to_string(current_state), logger::logLevel::DEBUG));

    if ((current_state & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED)
    {
        ret_value.push_back(update_definitions::Flags::OS);
    }

    current_state = completed_update.at(this->get_update_bit(update_definitions::Flags::APP, next_state)) - '0';

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
    
    if ( (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) == update_state.end()) && (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_UPDATE)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("pendingApplicationUpdate: is an application update pending? ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::pendingFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);
    
    if ( (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) && (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("pendingFirmwareUpdate: is a firmware update pending? ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::pendingApplicationFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);
    
    if ( (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) && (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("pendingApplicationFirmwareUpdate: is a firmware & application update pending? ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}


bool updater::Bootstate::failedFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(true);
    
    if ( (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) && (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("failedFirmwareUpdate: is a firmware update failed? ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::failedRebootFirmwareUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);

    if ( (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) && (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("failedRebootFirmwareUpdate: is a reboot firmware update failed? ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::failedApplicationUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(true);
    
    if ( (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) == update_state.end()) && (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::FAILED_APP_UPDATE)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("failedApplicationUpdate: is an application update failed? ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::missedFirmwareRebootDuringRollback()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);

    if ( (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end()) && (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING)
        {
            retValue = true;
        }
    }
    else
    {
        /* Check the state if last rollback can not be commited.
         * In this case the env. BOOT_A/B_LEFT = 0 and the system is back in
         * safe state.
         */
        const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
        const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

        const unsigned int number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
        const unsigned int number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

        const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
        const std::string current_slot = util::split(rauc_cmd, '=').back();

        /* check the case if BOOT_A/B_LEFT = 0 */
        if(this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b))
        {
            retValue = true;
            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
            if (number_of_tries_a == 0)
              {
                // mark a bad
                update.at (FIRMWARE_A_INDEX) = '0' + STATE_UPDATE_BAD;
              }
            else
              {
                if (number_of_tries_b == 0)
                  {
                    // mark a bad
                    update.at (FIRMWARE_B_INDEX) = '0' + STATE_UPDATE_BAD;
                  }
              }

              this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
              this->uboot_handler->flushEnvironment();
        }
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("missedFirmwareRebootDuringRollback: rollback has been processed, a reboot is mandatory: ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::missedApplicationRebootDuringRollback()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update(false);

    if ( (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()) && (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) == update_state.end()))
    {
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

        if (update_reboot_state == update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING)
        {
            retValue = true;
        }
    }

    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("missedApplicationRebootDuringRollback: rollback has been processed, a reboot is mandatory: ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}

void updater::Bootstate::confirmFailedFirmwareUpdate()
{
    if (this->failedFirmwareUpdate() == true)
    {
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(get_update_bit(update_definitions::Flags::OS, true)) = '0';
        
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_reboot_state));
        
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedFirmwareUpdate: failed firmware update is confirmed"), logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedFirmwareUpdate: no failed firmware update to confirm"),  logger::logLevel::ERROR));
        throw(ConfirmFailedFirmwareUpdate("no failed firmware update detected"));
    }
}

void updater::Bootstate::confirmFailedRebootFirmwareUpdate()
{
    if (this->failedRebootFirmwareUpdate() == true)
    {
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(get_update_bit(update_definitions::Flags::OS, false)) = '0';
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_reboot_state));

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedRebootFirmwareUpdate: failed update reboot firmware update is confirmed"), logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedRebootFirmwareUpdate: no failed update reboot firmware update to confirm"),  logger::logLevel::ERROR));
        throw(ConfirmFailedRebootFirmwareUpdate("no failed reboot firmware update detected"));
    }
}

void updater::Bootstate::confirmFailedApplicationeUpdate()
{
    if (this->failedApplicationUpdate() == true)
    {
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

        update.at(get_update_bit(update_definitions::Flags::APP, true)) = '0';
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_reboot_state));

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedApplicationeUpdate: failed application update is confirmed"), logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedApplicationeUpdate: no failed application update to confirm"),  logger::logLevel::ERROR));
        throw(ConfirmFailedApplicationUpdate("no failed application update detected"));
    }
}

void updater::Bootstate::confirmPendingFirmwareUpdate()
{
    if (this->pendingFirmwareUpdate())
    {
        const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
        const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: UBootEnv: Var.:\"BOOT_ORDER_OLD\": ") + boot_order_old, logger::logLevel::DEBUG));
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: UBootEnv: Var.:\"BOOT_ORDER\": ") + boot_order, logger::logLevel::DEBUG));

        const unsigned int number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
        const unsigned int number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: BootEnv: Var.:\"BOOT_A_LEFT\": ") + std::to_string(number_of_tries_a), logger::logLevel::DEBUG));
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: BootEnv: Var.:\"BOOT_B_LEFT\": ") + std::to_string(number_of_tries_b), logger::logLevel::DEBUG));

        const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
        const std::string current_slot = util::split(rauc_cmd, '=').back();

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: RAUC current slot: ") + current_slot, logger::logLevel::DEBUG));

        if (this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b))
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update failed"),  logger::logLevel::DEBUG));

            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED));
        }
        else if (this->missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b))
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update reboot missing"),  logger::logLevel::DEBUG));
        }
        else if (this->firmware_update_reboot_successful(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b))
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update successful"),  logger::logLevel::DEBUG));

            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
            update.at(get_update_bit(update_definitions::Flags::OS, false)) = '0';
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update state is illegal"),  logger::logLevel::ERROR));
            throw(FirmwareRebootStateNotDefined());
        }
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: No firmware update pending"), logger::logLevel::ERROR));
        throw(ConfirmPendingFirmwareUpdate("No pending firmware update"));
    }
}

void updater::Bootstate::confirmPendingApplicationUpdate()
{
    if (this->pendingApplicationUpdate())
    {
        const bool application_reboot = this->application_reboot();
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    
        if (application_reboot)
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "confirmPendingApplicationUpdate: mark application update as successful", logger::logLevel::DEBUG));
            update.at(get_update_bit(update_definitions::Flags::APP, false)) = '0';
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "confirmPendingApplicationUpdate: missing reboot for application update", logger::logLevel::ERROR));
        }
    }
}

void updater::Bootstate::confirmPendingApplicationFirmwareUpdate()
{
    if (this->pendingApplicationFirmwareUpdate())
    {
        const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
        const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: UBootEnv: Var.:\"BOOT_ORDER_OLD\": ") + boot_order_old, logger::logLevel::DEBUG));
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: UBootEnv: Var.:\"BOOT_ORDER\": ") + boot_order, logger::logLevel::DEBUG));

        const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
        const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: BootEnv: Var.:\"BOOT_A_LEFT\": ") + std::to_string(number_of_tries_a), logger::logLevel::DEBUG));
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: BootEnv: Var.:\"BOOT_B_LEFT\": ") + std::to_string(number_of_tries_b), logger::logLevel::DEBUG));

        const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
        const std::string current_slot = util::split(rauc_cmd, '=').back();

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: RAUC current slot: ") + current_slot, logger::logLevel::DEBUG));

        if (this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b))
        {
            const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
            update.at(get_update_bit(update_definitions::Flags::APP, false)) = '0';

            if (current_app == 'A')
            {
                this->uboot_handler->addVariable("application", "B");
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "confirmApplicationFirmwareUpdate: application rollback to B during failed app & fw update", logger::logLevel::DEBUG));
            }
            else
            {
                this->uboot_handler->addVariable("application", "A");
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "confirmApplicationFirmwareUpdate: application rollback to A during failed app & fw update", logger::logLevel::DEBUG));
            }

            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: firmware update failed"),  logger::logLevel::DEBUG));

            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED));
        }
        else if (this->missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b))
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: firmware update reboot missing"),  logger::logLevel::DEBUG));
        }
        else if (this->firmware_update_reboot_successful(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b))
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: firmware update successful"),  logger::logLevel::DEBUG));

            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

            update.at(get_update_bit(update_definitions::Flags::OS, false)) = '0';
            update.at(get_update_bit(update_definitions::Flags::APP, false)) = '0';
            
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update state is illegal"),  logger::logLevel::ERROR));
            throw(FirmwareRebootStateNotDefined());
        }
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: no firmware update pending"), logger::logLevel::ERROR));
        throw(ConfirmPendingFirmwareApplicationUpdate("No pending firmware and application update"));
    }

}

void updater::Bootstate::confirmMissedRebootDuringFirmwareRollback()
{
    std::ifstream fcmdline("/proc/cmdline", std::ifstream::in);
    CMDLINE_BOOTSTATE current_state;
    std::string cmdline;
    if (fcmdline.good())
    {
        std::getline(fcmdline, cmdline);
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: /proc/cmdline: ") + cmdline, logger::logLevel::DEBUG));
        if (std::regex_search(cmdline, booted_partition_A))
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: current booted A"), logger::logLevel::DEBUG));
            current_state = CMDLINE_BOOTSTATE::PARTITION_A;
        }
        else if (std::regex_search(cmdline, booted_partition_B))
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: current booted B"), logger::logLevel::DEBUG));
            current_state = CMDLINE_BOOTSTATE::PARTITION_B;
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: no partition A or B booted"), logger::logLevel::ERROR));
            throw(ReadCmdline("No A or B booted partition found"));
        }
    }
    else
    {
        if(fcmdline.eof())
        {
            const std::string error_msg = "End-of-File reached on input operation";
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: ") + error_msg, logger::logLevel::ERROR));
            throw(ReadCmdline(error_msg));
        }
        else if (fcmdline.fail())
        {
            const std::string error_msg = "Logical error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: ") + error_msg, logger::logLevel::ERROR));
            throw(ReadCmdline(error_msg));
        }
        else if (fcmdline.bad())
        {
            const std::string error_msg = "Read/writing error on I/O operation"; 
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: ") + error_msg, logger::logLevel::ERROR));
            throw(ReadCmdline(error_msg));
        }
    }

    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);
    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();
    
    const bool missing_reboot = ((number_of_tries_b  == 0) && (current_state == CMDLINE_BOOTSTATE::PARTITION_A)) ||\
                                ((number_of_tries_a  == 0) && (current_state == CMDLINE_BOOTSTATE::PARTITION_B));
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: current slot: ") + current_slot, logger::logLevel::DEBUG));
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: boot order old: ") + boot_order_old, logger::logLevel::DEBUG));
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: do we miss reboot: ") + std::to_string(missing_reboot), logger::logLevel::DEBUG));

    const bool frs = this->firmware_update_reboot_successful(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b);

    if (missing_reboot || frs)
    {
        const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(get_update_bit(update_definitions::Flags::OS, false)) = '0';
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        if(missing_reboot)
        {
            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
        } else if (frs)
        {
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
        }
        this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
        this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: reboot confirmed."), logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringFirmwareRollback: no reboot during rollback pending"), logger::logLevel::ERROR));
        throw(ConfirmMissedRebootDuringRollback());
    }
}

void updater::Bootstate::confirmMissedRebootDuringApplicationRollback()
{
    if (this->missedApplicationRebootDuringRollback() && !this->application_reboot())
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringApplicationRollback: no reboot during rollback pending"), logger::logLevel::ERROR));
        throw(ConfirmMissedRebootDuringRollback());
    }
    else if (this->missedApplicationRebootDuringRollback() && this->application_reboot())
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringApplicationRollback: reboot processed"), logger::logLevel::DEBUG));
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));

        update.at(get_update_bit(update_definitions::Flags::APP, false)) = '0';
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmMissedRebootDuringApplicationRollback: no reboot during rollback pending"), logger::logLevel::ERROR));
        throw(RollbackApplicationUpdate("No application reboot commit waiting"));
    }
}

bool updater::Bootstate::noUpdateProcessing()
{
    bool retValue = false;
    const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::to_UBootBootstateFlags(this->uboot_handler->getVariable("update_reboot_state", allowed_update_reboot_state_variables));

    if (update_reboot_state == update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING)
    {
        retValue = true;
    }
    
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("noUpdateProcessing: no update in process? ") + std::to_string(retValue), logger::logLevel::DEBUG));
    return retValue;
}

bool updater::Bootstate::firmware_update_reboot_failed(const std::string &current_slot,
    const std::string &boot_order_old,
    const std::string &boot_order,
    const uint8_t &number_of_tries_a,
    const uint8_t &number_of_tries_b)
{
    const bool ret_Value = (((current_slot == util::split(boot_order_old,' ').front()) && ((number_of_tries_a == 0) || (number_of_tries_b == 0))) && (boot_order_old != boot_order));
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("firmware_update_reboot_failed: ") + std::to_string(ret_Value), logger::logLevel::DEBUG));
    return ret_Value;
}

bool updater::Bootstate::firmware_update_reboot_successful(const std::string &current_slot,
    const std::string &boot_order_old,
    const std::string &boot_order,
    const uint8_t &number_of_tries_a,
    const uint8_t &number_of_tries_b)
{
    const bool ret_Value = ((current_slot == util::split(boot_order,' ').front()) && (boot_order_old != boot_order));
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("firmware_update_reboot_successful: ") + std::to_string(ret_Value), logger::logLevel::DEBUG));
    return ret_Value;
}

bool updater::Bootstate::missing_firmware_update_reboot(const std::string &current_slot,
    const std::string &boot_order_old,
    const std::string &boot_order,
    const uint8_t &number_of_tries_a,
    const uint8_t &number_of_tries_b)
{
    const bool ret_Value = ((current_slot != util::split(boot_order,' ').front()) && (number_of_tries_a == 3) && (number_of_tries_b == 3) && (boot_order_old != boot_order));
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("missing_firmware_update_reboot: ") + std::to_string(ret_Value), logger::logLevel::DEBUG));
    return ret_Value;
}

bool updater::Bootstate::application_reboot()
{
    bool application_reboot = false;
    std::ifstream mounted_devices("/sys/class/block/loop0/loop/backing_file", std::ifstream::in);
    if (mounted_devices.good())
    {
        do
        {
            std::string output;
            std::getline(mounted_devices, output);
            application_reboot = ((output.find("app_a.squashfs") != std::string::npos) && ('A' == this->uboot_handler->getVariable("application", allowed_application_variables)) ) ||
                                ((output.find("app_b.squashfs") != std::string::npos) && ('B' == this->uboot_handler->getVariable("application", allowed_application_variables)) );
        } while ( (mounted_devices.eof() == false) && (application_reboot == false) );

        if( (mounted_devices.eof() == true) && (application_reboot == false) )
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("application_reboot: No application image in /sys/class/block/loop0/loop/backing_file mounted"), logger::logLevel::DEBUG));
        }
    }
    else
    {
        if(mounted_devices.eof())
        {
            const std::string error_msg = "End-of-File reached on input operation";
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("application_reboot: ") + error_msg, logger::logLevel::ERROR));
            throw(GetLoopDevices(error_msg));
        }
        else if (mounted_devices.fail())
        {
            const std::string error_msg = "Logical error on I/O operation";
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("application_reboot: ") + error_msg, logger::logLevel::ERROR));
            throw(GetLoopDevices(error_msg));
        }
        else if (mounted_devices.bad())
        {
            const std::string error_msg = "Read/writing error on I/O operation"; 
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("application_reboot: ") + error_msg, logger::logLevel::ERROR));
            throw(GetLoopDevices(error_msg));
        }
    }
    return application_reboot;
}

void updater::Bootstate::firmware_rollback()
{
    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string boot_order     = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();

    const bool mfur = this->missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b);
    const bool fmrs = this->firmware_update_reboot_successful(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b);
    const bool furf = this->firmware_update_reboot_failed(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b);

    if (mfur)
    {
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(get_update_bit(update_definitions::Flags::OS, false)) = '0';
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
        this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
        this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("firmware_rollback: missing_firmware_update_reboot state, reset to old bootstate successful"), logger::logLevel::DEBUG));
    }
    else if (fmrs)
    {
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        if (current_slot == "A")
        {
            this->uboot_handler->addVariable("BOOT_A_LEFT", "0");
        }
        else
        {
            this->uboot_handler->addVariable("BOOT_B_LEFT", "0");
        }
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING));
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("firmware_rollback: firmware_update_reboot_successful state, reset to old bootstate successful"), logger::logLevel::DEBUG));
    }
    else if (furf)
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("firmware_rollback: Failed update reboot, a rollback is done"), logger::logLevel::WARNING));
    }
#if TODO
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("firmware_rollback: No allowed state"), logger::logLevel::ERROR));
        throw(RollbackFirmwareUpdate("No allowed state during update processed"));
    }
#endif
}

void updater::Bootstate::applicaton_rollback(updater::applicationUpdate &app_updater)
{
    if (this->pendingApplicationUpdate())
    {
        if(this->application_reboot())
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("applicaton_rollback: uncommited application -> reboot mandatory"), logger::logLevel::DEBUG));
            app_updater.rollback();
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING));
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("applicaton_rollback: uncommited application -> no reboot mandatory"), logger::logLevel::DEBUG));
            app_updater.rollback();
            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
            update.at(get_update_bit(update_definitions::Flags::APP, false)) = '0';
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("applicaton_rollback: no pending application update"), logger::logLevel::ERROR));
        throw(RollbackApplicationUpdate("No pending application update"));
    }
}

bool updater::Bootstate::firmware_reboot()
{
    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);
    const std::string boot_order     = this->uboot_handler->getVariable("BOOT_ORDER", allowed_boot_order_variables);

    const uint8_t number_of_tries_a = this->uboot_handler->getVariable("BOOT_A_LEFT", allowed_boot_ab_left_variables);
    const uint8_t number_of_tries_b = this->uboot_handler->getVariable("BOOT_B_LEFT", allowed_boot_ab_left_variables);

    const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
    const std::string current_slot = util::split(rauc_cmd, '=').back();

    return missing_firmware_update_reboot(current_slot, boot_order_old, boot_order, number_of_tries_a, number_of_tries_b);
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

    if(flag == update_definitions::Flags::OS)
    {
        const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd", allowed_rauc_cmd_variables);
        const std::string current_slot = util::split(rauc_cmd, '=').back();
        if ((current_slot == "B") && (next == false))
        {
            updatebit_index = FIRMWARE_B_INDEX;
        } else
        {
            if((current_slot == "A") && (next == true))
            {
                updatebit_index = FIRMWARE_B_INDEX;
            }
        }
    } else {
        const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);
        updatebit_index = APPLICATION_A_INDEX;
        if ((current_app == 'B') && (next == false))
        {
            updatebit_index = APPLICATION_B_INDEX;
        } else
        {
            if ((current_app == 'A') && (next == true))
            {
                updatebit_index = APPLICATION_B_INDEX;
            }
        }
    }

    return updatebit_index;
}
