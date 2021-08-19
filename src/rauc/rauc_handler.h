#pragma once

#include "../subprocess/subprocess.h"
#include "../uboot_interface/UBoot.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include <json/json.h>
#include <string>
#include <filesystem>
#include <exception>
#include <memory>


constexpr char RAUC_DOMAIN[] = "RAUC";

/**
 * Helper module to control the RAUC updater.
 * The different commands are abstracted and offered through this class.
 */
namespace rauc
{
    ///////////////////////////////////////////////////////////////////////////
    /// rauc' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Base class of exception class from helper module.
     * All exceptions of RAUC are derived from this base class.
     * RAUC-Errors are saved in variable "error_report".
     */
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
            /**
             * Describes when the reported JSON-String from RAUC could not be parsed as a valid
             * JSON-String.
             * @param error_msg Reason for error during parsing.
             */
            explicit ErrorParseJson(const std::string & error_msg)
            {
                this->error_msg = std::string("Could not parse JSON: \"") + error_msg + std::string("\"");
            }
    };

    class MarkUBootEnv : public RaucBaseException
    {
        public:
            /**
             * Can not activated or deactivate the partition which contains the UBoot-Environment.
             * @param error_string Error string of problem while writing
             * @param mark_active Activate/Deactivate the UBoot-Env. partition.
             */
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
            /**
             * Can not install the RAUC bundle on the system.
             * @param bundle_path Path to the RAUC artifact.
             * @param error_report Report of RAUC installer to the failed install attempt.
             */
            RaucInstallBundle(const std::filesystem::path & bundle_path, const std::string & error_report)
            {
                this->error_msg = std::string("Error during install of image: \"") + bundle_path.string() + std::string("\"");
                this->error_report = error_report;
            }
    };

    class RaucGetArtifactInformation : public RaucBaseException
    {
        public:
            /**
             * Error during gaining information from a given artifact.
             * @param bundle_path Path to the RAUC artifact.
             * @param error_report Report of RAUC installer to the failed read attempt.
             */
            RaucGetArtifactInformation(const std::filesystem::path & bundle_path, const std::string & error_report)
            {
                this->error_msg = std::string("Error during gaining information: \"") + bundle_path.string() + std::string("\"");
                this->error_report = error_report;
            }
    };

    class RaucMarkOtherPartition : public RaucBaseException
    {
        public:
            /**
             * Can not mark alternative partition as active, bootable.
             * @param error_report Report of RAUC installer on failure.
             */
            explicit RaucMarkOtherPartition(const std::string & error_report)
            {
                this->error_msg = std::string("Error during marking other image");
                this->error_report = error_report;
            }
    };

    class RaucRollback : public RaucBaseException
    {
        public:
            /**
             * Can not rollback to the old software version.
             * @param error_report Report during rollback from RAUC.
             */
            explicit RaucRollback(const std::string & error_report)
            {
                this->error_msg = std::string("Error during rollback");
                this->error_report = error_report;
            }
    };

    class RaucGetStatus : public RaucBaseException
    {
        public:
            /**
             * Error during attempt get status with RAUC.
             * @param error_report Problem to get status with RAUC.
             */
            explicit RaucGetStatus(const std::string & error_report)
            {
                this->error_msg = std::string("Error during getting status");
                this->error_report = error_report;
            }
    };

    class RaucMarkUpdateAsSuccessfull : public RaucBaseException
    {
        public:
            /**
             * Could not mark update as successful.
             */
            RaucMarkUpdateAsSuccessfull()
            {
                this->error_msg = std::string("Error mark update as successful");
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
            /**
             * Constructor of RAUC interface. Needs UBoot-interface as a shared medium and also a logger interface.
             * Both are configured & instanced outside but used by the object.
             * @param prt UBoot::UBoot shared medium between multiple objects.
             * @param logger logger::LoggerHandler global logger instace
             * @throw MarkUBootEnv Excepts if rauc_handler is not able to make eMMC or NAND UBoot environment as writeable.
             */
            rauc_handler(const std::shared_ptr<UBoot::UBoot> &, const std::shared_ptr<logger::LoggerHandler> &);

            ~rauc_handler();

            /**
             * Start RAUC install process for given artifact.
             * @param path_to_bundle Path to RAUC install artifact.
             * @throw RaucInstallBundle When rauc failed with install process.
             */
            void installBundle(const std::filesystem::path &);

            /**
             * After an update you can confirm the update with the rauc functionality
             * @throw RaucMarkUpdateAsSuccessfull
             */
            void markUpdateAsSuccessfull();

            /**
             * Return the information that can be read from the given RAUC install artifact.
             * @param path_to_bundle Path to the RAUC artifact.
             * @return JSON object which represent the return value.
             * @throw RaucGetArtifactInformation
             */
            Json::Value getInfoAboutAboutBundle(std::filesystem::path &);

            /**
             * Mark alternative partition as good.
             * @throw RaucMarkOtherPartition
             */
            void markOtherPartition();

            /**
             * Mark alternative partition as good and active.
             * @throw RaucRollback
             */
            void rollback();

            /**
             * Get the status of RAUC.
             * @throw RaucGetStatus
             * @return JSON object which represent the return value.
             */
            Json::Value getStatus();
    };
}