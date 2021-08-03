#ifndef RAUC_HANDLER_H
#define RAUC_HANDLER_H

#include "../subprocess/subprocess.h"
#include "../uboot_interface/UBoot.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include <inicpp/inicpp.h>
#include <json/json.h>
#include <string>
#include <sstream>
#include <filesystem>
#include <exception>
#include <memory>
#include <fstream>

constexpr char RAUC_DOMAIN[] = "RAUC";


namespace rauc
{
    ///////////////////////////////////////////////////////////////////////////
    /// rauc' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    class RaucBaseException : public std::exception
    {
        protected:
            std::string error_msg;
            std::string error_report;
        
        public:
            const char * what() const throw () 
            {
                return this->error_msg.c_str();
            }

            const std::string report() const
            {
                return this->error_report;
            }
    };

    class ErrorParseJson : public RaucBaseException
    {       
        public:
            explicit ErrorParseJson(const std::string & error_msg)
            {
                this->error_msg = std::string("Could not parse JSON: \"") + error_msg + std::string("\"");
            }
    };

    class MarkUBootEnv : public RaucBaseException
    {
        public:
            MarkUBootEnv(const std::string error_string, bool mark_active)
            {
                if (mark_active)
                {
                    this->error_msg = std::string("Error during allow writing UBoot Env: ") + error_string;
                }
                else
                {
                    this->error_msg = std::string("Error during set read-only UBoot Env: ") + error_string;
                }
            }
    };

    class RaucInstallBundle : public RaucBaseException
    {
        public:
            RaucInstallBundle(const std::filesystem::path & bundle_path, const std::string & error_report)
            {
                this->error_msg = std::string("Error during install of image: \"") + bundle_path.string() + std::string("\"");
                this->error_report = error_report;
            }
    };

    class RaucMarkOtherPartition : public RaucBaseException
    {
        public:
            explicit RaucMarkOtherPartition(const std::string & error_report)
            {
                this->error_msg = std::string("Error during marking other image");
                this->error_report = error_report;
            }
    };

    class RaucRollback : public RaucBaseException
    {
        public:
            explicit RaucRollback(const std::string & error_report)
            {
                this->error_msg = std::string("Error during rollback");
                this->error_report = error_report;
            }
    };

    class RaucGetStatus : public RaucBaseException
    {
        public:
            explicit RaucGetStatus(const std::string & error_report)
            {
                this->error_msg = std::string("Error during getting status");
                this->error_report = error_report;
            }
    };

    class RaucMarkUpdateAsSuccessfull : public RaucBaseException
    {
        public:
            RaucMarkUpdateAsSuccessfull()
            {
                this->error_msg = std::string("Error mark update as successful");
            }
    };
 
    class UBootEnvAccess : public RaucBaseException
    {
        public:
            UBootEnvAccess()
            {
                this->error_msg = "Error access UBoot Env access";
            }
    };

    ///////////////////////////////////////////////////////////////////////////
    /// rauc declaration
    ///////////////////////////////////////////////////////////////////////////
    
    enum class memory_type
    {
        eMMC,
        NAND,
        None
    };
 

    class rauc_handler
    {
        private:
            const std::string rauc_install_cmd, rauc_info_cmd, rauc_status, 
                              rauc_mark_good, rauc_mark_good_other, rauc_rollback;

            std::shared_ptr<UBoot::UBoot> uboot_handler;
            std::shared_ptr<logger::LoggerHandler> logger;

            memory_type current_uboot_env_memory() noexcept;
        public:
            rauc_handler(const std::shared_ptr<UBoot::UBoot> &, const std::shared_ptr<logger::LoggerHandler> &);
            ~rauc_handler();
            void installBundle(const std::filesystem::path &);
            void markUpdateAsSuccessfull();
            Json::Value getInfoAboutAboutBundle(std::filesystem::path &);
            void markOtherPartition();
            void rollback();
            Json::Value getStatus();
    };
};

#endif