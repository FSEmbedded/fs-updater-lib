#pragma once

#include "../subprocess/subprocess.h"
#include "../uboot_interface/UBoot.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "rauc_exceptions.h"

#include <json/forwards.h> // forward-declares Json::Value; full <json/json.h> lives in the .cpp
#include <string>
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
            const std::string rauc_install_cmd, rauc_status,
                              rauc_mark_good_other, rauc_rollback;

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
            rauc_handler(const std::shared_ptr<UBoot::UBoot> & /*ptr*/, const std::shared_ptr<logger::LoggerHandler> & /*logger*/);

            ~rauc_handler();

            /**
             * Start RAUC install process for given artifact.
             * @param path_to_bundle Path to RAUC install artifact.
             * @throw RaucInstallBundle When rauc failed with install process.
             */
            void installBundle(const std::string & /*path_to_bundle*/);

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