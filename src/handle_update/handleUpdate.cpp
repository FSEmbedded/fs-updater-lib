#include "handleUpdate.h"

updater::Bootstate::Bootstate(const std::shared_ptr<UBoot::UBoot> & ptr):
    uboot_handler(ptr)
{

}

const std::vector<update_definitions::Flags> updater::Bootstate::get_complete_update()
{
    const std::string completed_update = this->uboot_handler->getVariable("update");
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

bool updater::Bootstate::checkPendingUpdate()
{
    std::vector<update_definitions::Flags> update_state = this->get_complete_update();
    std::vector<unsigned char> update = util::to_array(this->uboot_handler->getVariable("update"));

    bool retValue = false;
    if (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end())
    {
        const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD");
        const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER");

        const std::string rauc_cmd = this->uboot_handler->getVariable("rauc_cmd");
        const std::string current_slot = util::split(rauc_cmd, '=').at(0);

        const std::string update_reboot_state = this->uboot_handler->getVariable("update_reboot_state");

        const unsigned int number_of_tries_a = std::stoul(this->uboot_handler->getVariable("BOOT_A_LEFT"));
        const unsigned int number_of_tries_b = std::stoul(this->uboot_handler->getVariable("BOOT_B_LEFT"));

        if (
            (
                (current_slot == util::split(boot_order,' ').at(0)) ||
                (number_of_tries_a == 0) || (number_of_tries_b == 0)
            ) &&
            (
                update_reboot_state == update_definitions::to_string(update_definitions::UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
            ) &&
            (
                boot_order_old == boot_order
            ) &&
            (
                (this->uboot_handler->getVariable("update_reboot_state") == update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)) ||
                (this->uboot_handler->getVariable("update_reboot_state") == update_definitions::to_string(update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED))
            )
        )
        {
            if ((number_of_tries_a != 0) && (number_of_tries_b != 0))
            {
                update[2] = '0';
                this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
                this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
                retValue = true;
            }
            else
            {
                this->uboot_handler->addVariable("BOOT_ORDER", boot_order_old);
            }
        }
        else if (
            (current_slot == util::split(boot_order_old, ' ').at(0)) &&
            (
                (number_of_tries_a == 0) ||
                (number_of_tries_b == 0) 
            ) &&
            (
                update.at(2) == '1'
            ) &&
            (
                boot_order == boot_order_old
            ) &&
            (
                (update_reboot_state == update_definitions::to_string(update_definitions::UBootBootstateFlags::FAILED_FW_UPDATE)) ||
                (update_reboot_state == update_definitions::to_string(update_definitions::UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED))
            )
        )
        {
            update[2] = '0';
            uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
            retValue = true;
        }
    }


    if (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end())
    {
        subprocess::Popen handle("losetup -a");

        if (handle.returnvalue() != 0)
        {
            throw(ErrorGetLoopDevices());
        }

        const std::string output = handle.output();

        if (
                (
                    (output.find("app_a.squashfs") != std::string::npos) &&
                    ("A" == this->uboot_handler->getVariable("application")) 
                ) ||
                (
                    (output.find("app_b.squashfs") != std::string::npos) &&
                    ("B" == this->uboot_handler->getVariable("application")) 
                )

        )
        {
            update[3] = '0';
            this->uboot_handler->addVariable("update_reboot_state", update_definitions::to_string(update_definitions::UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING));
            retValue = true;
        }
    }

    if (
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::APP) != update_state.end()) ||
        (std::find(update_state.begin(), update_state.end(), update_definitions::Flags::OS) != update_state.end())
    )
    {
        this->uboot_handler->addVariable("update", std::string(update.begin(), update.end()));
        this->uboot_handler->flushEnvironment();
    }

    return retValue;
}
