#include "rauc_handler.h"

#include <inicpp/inicpp.h>
#include <fstream>
#include <sstream>

rauc::memory_type rauc::rauc_handler::current_uboot_env_memory() noexcept
{
    std::ifstream uboot_env(UBOOT_CONFIG_PATH);
    if (uboot_env.good())
    {
        while(!uboot_env.eof())
        {
            std::string output;
            std::getline(uboot_env, output);
            
            std::string memory_regex_emmc("mmcblk2boot0");
            std::string memory_regex_nand("mtd2");

            if (output.find(memory_regex_emmc) != std::string::npos)
            {
                this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("current_uboot_env_memory: detect eMMC UBoot env."), logger::logLevel::DEBUG));
                return memory_type::eMMC;
            } 
            else if (output.find(memory_regex_nand) != std::string::npos)
            {
                this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("current_uboot_env_memory: detect NAND UBoot env."), logger::logLevel::DEBUG));
                return memory_type::NAND;
            }
        }
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("current_uboot_env_memory: could not detect UBoot env."), logger::logLevel::DEBUG));
        return memory_type::None;
    }
    else
    {
        const std::string error_msg = "Error during access";
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("current_uboot_env_memory: ") + error_msg, logger::logLevel::ERROR));
        return memory_type::None;
    }
}

rauc::rauc_handler::rauc_handler(const std::shared_ptr<UBoot::UBoot> &ptr, const std::shared_ptr<logger::LoggerHandler> &logger): 
    rauc_install_cmd("rauc install "),
    rauc_info_cmd("rauc info --output-format=json "),
    rauc_status("rauc status --output-format=json"),
    rauc_mark_good("rauc status --output-format=json mark-good"),
    rauc_mark_good_other("rauc status --output-format=json mark-good other"),
    rauc_rollback("rauc status --output-format=json mark-active other"),
    uboot_handler(ptr),
    logger(logger)
{
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, "handler constructed", logger::logLevel::DEBUG));

    const std::string force_ro = "/sys/block/mmcblk2boot0/force_ro";
    std::ofstream uboot_acc(force_ro, std::ios::app);
    if (this->current_uboot_env_memory() == memory_type::eMMC)
    {
        if (uboot_acc.good())
        {
            uboot_acc.write("0",1);
        }
        else
        {
            if(uboot_acc.eof())
            {
                const std::string error_msg = "End-of-File reached on input operation";
                this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
                throw(MarkUBootEnv(error_msg, true));
            }
            else if (uboot_acc.fail())
            {
                const std::string error_msg = "Logical error on i/o operation";
                this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
                throw(MarkUBootEnv(error_msg, true));
            }
            else if (uboot_acc.bad())
            {
                const std::string error_msg = "Read/writing error on i/o operation"; 
                this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getCurrentVersion: ") + error_msg, logger::logLevel::ERROR));
                throw(MarkUBootEnv(error_msg, true));
            }
        }
    } 
}  

rauc::rauc_handler::~rauc_handler()
{
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, "handler deconstructed", logger::logLevel::DEBUG));
    if (this->current_uboot_env_memory() == memory_type::eMMC)
    {
        const std::string force_ro = "/sys/block/mmcblk2boot0/force_ro";
        std::ofstream uboot_acc(force_ro, std::ios::app);
        if (uboot_acc.good())
        {
            uboot_acc.write("1",1);
        }
    }
}

void rauc::rauc_handler::installBundle(const std::filesystem::path & path_to_bundle)
{
    std::string command = this->rauc_install_cmd + std::string(path_to_bundle);
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("installBundle: execute cmd: ") + command, logger::logLevel::DEBUG));
    subprocess::Popen handler = subprocess::Popen(command);
    if ( handler.returnvalue() != 0 )
    {
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("installBundle: error during execution: ") + handler.output(), logger::logLevel::ERROR));
        throw(RaucInstallBundle(path_to_bundle, handler.output()));
    }
}

void rauc::rauc_handler::markUpdateAsSuccessfull()
{
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("markUpdateAsSuccessfull: execute cmd: ") + this->rauc_mark_good, logger::logLevel::DEBUG));
    subprocess::Popen handler = subprocess::Popen(this->rauc_mark_good);
    if ( handler.returnvalue() != 0 )
    {
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("markUpdateAsSuccessfull: error during execution: ") + handler.output(), logger::logLevel::ERROR));
        throw(RaucMarkUpdateAsSuccessfull());
    }

    const std::string boot_order = this->uboot_handler->getVariable("BOOT_ORDER");
    const std::string boot_order_old = this->uboot_handler->getVariable("BOOT_ORDER_OLD");
    
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("markUpdateAsSuccessfull: U-Boot env: BOOT_ORDER=") + boot_order, logger::logLevel::DEBUG));
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("markUpdateAsSuccessfull: U-Boot env: BOOT_ORDER_OLD=") + boot_order_old, logger::logLevel::DEBUG));

    if (boot_order != boot_order_old)
    {
        this->uboot_handler->addVariable("BOOT_ORDER", boot_order);
    }
}

Json::Value rauc::rauc_handler::getInfoAboutAboutBundle(std::filesystem::path & path_to_bundle)
{   
    std::string command = this->rauc_info_cmd + std::string(path_to_bundle);
    
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getInfoAboutAboutBundle: execute cmd: ") + this->rauc_info_cmd, logger::logLevel::DEBUG));
    subprocess::Popen handler = subprocess::Popen(command);
    
    if ( handler.returnvalue() != 0 )
    {
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getInfoAboutAboutBundle: error during execution: ") + handler.output(), logger::logLevel::ERROR));
        throw(RaucGetArtifactInformation(path_to_bundle, handler.output()));
    }

    Json::CharReaderBuilder reader;
    Json::Value value;
    std::string errs;
    std::stringstream json_input;
    json_input << handler.output();
    const bool status_reader = Json::parseFromStream(reader, json_input, &value, &errs);
    if (status_reader == false)
    {
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getInfoAboutAboutBundle: error during parsing JSON ") + errs, logger::logLevel::ERROR));
        throw(ErrorParseJson(std::string("Wrong JSON format: ") + errs));
    }

    return value;
}

void rauc::rauc_handler::markOtherPartition()
{
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("markOtherPartition: execute cmd: ") + this->rauc_mark_good_other, logger::logLevel::DEBUG));
    subprocess::Popen handler = subprocess::Popen(this->rauc_mark_good_other);
    if (handler.returnvalue() != 0)
    {
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("markOtherPartition: error during execution: ") + handler.output(), logger::logLevel::ERROR));
        throw(RaucMarkOtherPartition(handler.output()));
    }
}

void rauc::rauc_handler::rollback()
{
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("rollback: execute cmd: ") + this->rauc_rollback, logger::logLevel::DEBUG));
    subprocess::Popen handler_rollback = subprocess::Popen(this->rauc_rollback);
    if (handler_rollback.returnvalue() != 0)
    {
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("rollback: error during execution: ") + handler_rollback.output(), logger::logLevel::ERROR));   
        throw(RaucRollback(handler_rollback.output()));
    }

    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("rollback: execute cmd: ") + this->rauc_mark_good_other, logger::logLevel::DEBUG));
    subprocess::Popen handler_mark_good_other = subprocess::Popen(this->rauc_mark_good_other);
    if (handler_mark_good_other.returnvalue() != 0)
    {
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("rollback: error during execution: ") + handler_mark_good_other.output(), logger::logLevel::ERROR));   
        throw(RaucMarkOtherPartition(handler_mark_good_other.output()));
    }
}

Json::Value rauc::rauc_handler::getStatus()
{
    this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getStatus: execute cmd: ") + this->rauc_rollback, logger::logLevel::DEBUG));
    subprocess::Popen handler = subprocess::Popen(this->rauc_status);
    if (handler.returnvalue() != 0)
    {
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getStatus: error during execution: ") + handler.output(), logger::logLevel::ERROR));   
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
        this->logger->setLogEntry(logger::LogEntry(RAUC_DOMAIN, std::string("getStatus: error during parsing JSON ") + errs, logger::logLevel::ERROR));
        throw(ErrorParseJson(std::string("Wrong JSON format: ") + errs));
    }

    return value;
}

