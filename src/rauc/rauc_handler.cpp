#include "rauc_handler.h"

rauc::rauc_handler::rauc_handler(const std::shared_ptr<UBoot::UBoot> &ptr): 
    rauc_install_cmd("rauc install "),
    rauc_info_cmd("rauc info --output-format=json "),
    rauc_status("rauc status --output-format=json "),
    rauc_mark_good("rauc status --output-format=json mark-good "),
    rauc_mark_good_other("rauc status --output-format=json mark-good other "),
    rauc_rollback("rauc status --output-format=json mark-active other "),
    uboot_handler(ptr)
{

}

rauc::rauc_handler::~rauc_handler()
{

}

void rauc::rauc_handler::installBundle(const std::filesystem::path & path_to_bundle)
{
    std::string command = this->rauc_install_cmd + std::string(path_to_bundle);
    subprocess::Popen handler = subprocess::Popen(command);
    if ( handler.returnvalue() != 0 )
    {
        throw(RaucInstallBundle(path_to_bundle, handler.output()));
    }
}

void rauc::rauc_handler::markUpdateAsSuccessfull()
{
    subprocess::Popen handler = subprocess::Popen(this->rauc_mark_good);

    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER");
    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD");

    if (boot_order != boot_order_old)
    {
        this->uboot_handler->addVariable("BOOT_ORDER", boot_order);
    }
}

Json::Value rauc::rauc_handler::getInfoAboutAboutBundle(std::filesystem::path & path_to_bundle)
{   
    std::string command = this->rauc_info_cmd + std::string(path_to_bundle);
    subprocess::Popen handler = subprocess::Popen(command);
    
    if ( handler.returnvalue() != 0 )
    {
        throw(RaucInstallBundle(path_to_bundle, handler.output()));
    }

    Json::CharReaderBuilder reader;
    Json::Value value;
    std::string errs;
    std::stringstream json_input;
    json_input << handler.output();
    const bool status_reader = Json::parseFromStream(reader, json_input, &value, &errs);
    if (status_reader == false)
    {
        throw(ErrorParseJson(std::string("Wrong JSON format: ") + errs));
    }

    return value;
}

void rauc::rauc_handler::markOtherPartition()
{
    subprocess::Popen handler = subprocess::Popen(this->rauc_mark_good_other);
    if (handler.returnvalue() != 0)
    {
        throw(RaucMarkOtherPartition(handler.output()));
    }
}

void rauc::rauc_handler::rollback()
{
    subprocess::Popen handler_rollback = subprocess::Popen(this->rauc_rollback);
    if (handler_rollback.returnvalue() != 0)
    {
        throw(RaucRollback(handler_rollback.output()));
    }

    subprocess::Popen handler_mark_good_other = subprocess::Popen(this->rauc_mark_good_other);
    if (handler_mark_good_other.returnvalue() != 0)
    {
        throw(RaucMarkOtherPartition(handler_mark_good_other.output()));
    }
}

Json::Value rauc::rauc_handler::getStatus()
{
    subprocess::Popen handler = subprocess::Popen(this->rauc_status);
    if (handler.returnvalue() != 0)
    {
        throw(RaucGetStatus(handler.output()));
    }

    Json::CharReaderBuilder reader;
    Json::Value value;
    std::string errs;
    std::stringstream json_input;
    json_input << handler.output();

    const bool status_reader = Json::parseFromStream(reader, json_input, &value, &errs);
    if (status_reader == false)
    {
        throw(ErrorParseJson(std::string("Wrong JSON format: ") + errs));
    }

    return value;
}

