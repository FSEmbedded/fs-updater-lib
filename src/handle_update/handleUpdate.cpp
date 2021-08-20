#include "handleUpdate.h"

#include "utils.h"
#include "../uboot_interface/allowed_uboot_variable_states.h"
#include <algorithm>
#include <fstream>


updater::Bootstate::Bootstate(const std::shared_ptr<UBoot::UBoot> & ptr, const std::shared_ptr<logger::LoggerHandler> & logger):
    uboot_handler(ptr),
    logger(logger)
{
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "bootstate: constructor", logger::logLevel::DEBUG));
}

updater::Bootstate::~Bootstate()
{
    this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "bootstate: deconstruct", logger::logLevel::DEBUG));
}

const std::vector<update_definitions::Flags> updater::Bootstate::get_complete_update()
{
    const std::string completed_update = this->uboot_handler->getVariable("update", allowed_update_variables);
    std::vector<update_definitions::Flags> ret_value;

    if (completed_update.at(2) == '1')
    {
        ret_value.push_back(update_definitions::Flags::OS);
    }

    if (completed_update.at(3) == '1')
    {
        ret_value.push_back(update_definitions::Flags::APP);
    }

    return ret_value;
}

bool updater::Bootstate::pendingApplicationUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update();
    
    if (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end())
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
    std::vector<update_definitions::Flags> update_state = this->get_complete_update();
    
    if (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end())
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
    std::vector<update_definitions::Flags> update_state = this->get_complete_update();
    
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
    std::vector<update_definitions::Flags> update_state = this->get_complete_update();
    
    if (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end())
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

bool updater::Bootstate::failedApplicationUpdate()
{
    bool retValue = false;
    std::vector<update_definitions::Flags> update_state = this->get_complete_update();
    
    if (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end())
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

void updater::Bootstate::confirmFailedFirmwareUpdate()
{
    if (this->failedFirmwareUpdate() == true)
    {
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(2) = '0';
        
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_reboot_state));
        
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedFirmwareUpdate: failed firmware update is confirmed"), logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedFirmwareUpdate: no failed firmware update to confirm"),  logger::logLevel::ERROR));
        throw(ErrorConfirmFailedFirmwareUpdate("no failed firmware update detected"));
    }
}

void updater::Bootstate::confirmFailedApplicationeUpdate()
{
    if (this->failedApplicationUpdate() == true)
    {
        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
        update.at(3) = '0';
        
        const update_definitions::UBootBootstateFlags update_reboot_state = update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;

        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_reboot_state));

        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedApplicationeUpdate: failed application update is confirmed"), logger::logLevel::DEBUG));
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmFailedApplicationeUpdate: no failed application update to confirm"),  logger::logLevel::ERROR));
        throw(ErrorConfirmFailedApplicationUpdate("no failed application update detected"));
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

        const bool firmware_update_reboot_failed = ((current_slot == util::split(boot_order_old,' ').front()) && ((number_of_tries_a == 0) || (number_of_tries_b == 0))) && (boot_order_old != boot_order);
        const bool firmware_update_reboot_successful = (current_slot == util::split(boot_order,' ').front()) && (boot_order_old != boot_order);
        const bool missing_firmware_update_reboot = (current_slot != util::split(boot_order,' ').front()) && (number_of_tries_a == 3) && (number_of_tries_b == 3) && (boot_order_old != boot_order);

        if (firmware_update_reboot_failed)
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update failed"),  logger::logLevel::DEBUG));

            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE));
        }
        else if (missing_firmware_update_reboot)
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update reboot missing"),  logger::logLevel::DEBUG));
        }
        else if (firmware_update_reboot_successful)
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update successful"),  logger::logLevel::DEBUG));

            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
            update.at(2) = '0';
            
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update state is illegal"),  logger::logLevel::ERROR));
            throw(ErrorFirmwareRebootStateNotDefined());
        }
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: no firmware update pending"), logger::logLevel::ERROR));
        throw(ErrorConfirmPendingFirmwareUpdate("No pending firmware update"));
    }
}

void updater::Bootstate::confirmPendingApplicationUpdate()
{
    if (this->pendingApplicationUpdate())
    {
        bool application_reboot = false;
        std::ifstream mounted_devices("/sys/class/block/loop0/loop/backing_file");
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
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingApplicationUpdate: No application image in /sys/class/block/loop0/loop/backing_file mounted"), logger::logLevel::ERROR));
            }
        }
        else
        {
            if(mounted_devices.eof())
            {
                const std::string error_msg = "End-of-File reached on input operation";
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingApplicationUpdate: ") + error_msg, logger::logLevel::ERROR));
                throw(ErrorGetLoopDevices(error_msg));
            }
            else if (mounted_devices.fail())
            {
                const std::string error_msg = "Logical error on I/O operation";
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingApplicationUpdate: ") + error_msg, logger::logLevel::ERROR));
                throw(ErrorGetLoopDevices(error_msg));
            }
            else if (mounted_devices.bad())
            {
                const std::string error_msg = "Read/writing error on I/O operation"; 
                this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingApplicationUpdate: ") + error_msg, logger::logLevel::ERROR));
                throw(ErrorGetLoopDevices(error_msg));
            }
        }

        std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
    
        if (application_reboot)
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, "confirmPendingApplicationUpdate: mark application update as successful", logger::logLevel::DEBUG));
            update.at(3) = '0';
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

        const bool firmware_update_reboot_failed = ((current_slot == util::split(boot_order_old,' ').front()) && ((number_of_tries_a == 0) || (number_of_tries_b == 0))) && (boot_order_old != boot_order);
        const bool firmware_update_reboot_successful = (current_slot == util::split(boot_order,' ').front()) && (boot_order_old != boot_order);
        const bool missing_firmware_update_reboot = (current_slot != util::split(boot_order,' ').front()) && (number_of_tries_a == 3) && (number_of_tries_b == 3) && (boot_order_old != boot_order);

        if (firmware_update_reboot_failed)
        {
            const char current_app = this->uboot_handler->getVariable("application", allowed_application_variables);

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

            this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE));
        }
        else if (missing_firmware_update_reboot)
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: firmware update reboot missing"),  logger::logLevel::DEBUG));
        }
        else if (firmware_update_reboot_successful)
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmApplicationFirmwareUpdate: firmware update successful"),  logger::logLevel::DEBUG));

            std::vector<uint8_t> update = util::to_array(this->uboot_handler->getVariable("update", allowed_update_variables));
            update.at(3) = '0';
            update.at(2) = '0';

            
            this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
            this->uboot_handler->addVariable("BOOT_ORDER_OLD", boot_order);
            this->uboot_handler->addVariable("BOOT_A_LEFT", "3");
            this->uboot_handler->addVariable("BOOT_B_LEFT", "3");
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
        }
        else
        {
            this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: firmware update state is illegal"),  logger::logLevel::ERROR));
            throw(ErrorFirmwareRebootStateNotDefined());
        }
    }
    else
    {
        this->logger->setLogEntry(logger::LogEntry(BOOTSTATE_DOMAIN, std::string("confirmPendingFirmwareUpdate: no firmware update pending"), logger::logLevel::ERROR));
        throw(ErrorConfirmPendingFirmwareApplicationUpdate("No pending firmware and application update"));
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