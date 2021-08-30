#pragma once

#include "updateDefinitions.h"
#include "../uboot_interface/UBoot.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include <filesystem>
#include <memory>
#include <string>

constexpr char BASE_UPDATE[] = "base update";

namespace updater
{
    ///////////////////////////////////////////////////////////////////////////
    /// firmwareUpdate declaration
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Base class of Updater. Interface all needed functions.
     */
    class updateBase
    {
        protected:
            std::shared_ptr<UBoot::UBoot> uboot_handler;
            std::shared_ptr<logger::LoggerHandler> logger;

        public:
            updateBase(const std::shared_ptr<UBoot::UBoot> &, const std::shared_ptr<logger::LoggerHandler> &);
            ~updateBase();
            
            updateBase(const updateBase &) = delete;
            updateBase &operator=(const updateBase &) = delete;
            updateBase(updateBase &&) = delete;
            updateBase &operator=(updateBase &&) = delete;

            /**
             * Abstract install function. Interface for install image.
             * @param path_object
             */
            virtual void install(const std::filesystem::path &) = 0;

            /**
             * Abstract rollback function. Interface for roll to next older version back.
             */
            virtual void rollback() = 0;

            /**
             * Return current software version.
             * @return Software version of current state.
             */
            virtual unsigned int getCurrentVersion() = 0;
    };
}