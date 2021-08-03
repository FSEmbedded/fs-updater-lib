#ifndef UPDATE_BASE_H
#define UPDATE_BASE_H

#include "updateDefinitions.h"
#include "../uboot_interface/UBoot.h"

#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include "./../BaseException.h"

#include <filesystem>
#include <memory>
#include <exception>
#include <string>

constexpr char BASE_UPDATE[] = "base update";

namespace updater
{
    ///////////////////////////////////////////////////////////////////////////
    /// updateBase' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    class UpdateProcessRunning : public fs::BaseFSUpdateException
    {
        public:
            explicit UpdateProcessRunning(update_definitions::UBootBootstateFlags flag)
            {
                this->error_msg = std::string("Not allowed to update, updating: ") + update_definitions::to_string(flag);
            }
    };

    ///////////////////////////////////////////////////////////////////////////
    /// firmwareUpdate declaration
    ///////////////////////////////////////////////////////////////////////////
    class updateBase
    {
        protected:
            std::shared_ptr<UBoot::UBoot> uboot_handler;
            std::shared_ptr<logger::LoggerHandler> logger;

        public:
            explicit updateBase(const std::shared_ptr<UBoot::UBoot> &, const std::shared_ptr<logger::LoggerHandler> &);
            ~updateBase();
            
            updateBase(const updateBase &) = delete;
            updateBase &operator=(const updateBase &) = delete;
            updateBase(updateBase &&) = delete;
            updateBase &operator=(updateBase &&) = delete;

            virtual void install(const std::filesystem::path &) = 0;
            virtual void rollback() = 0;
            virtual unsigned int getCurrentVersion() = 0;
    };
};

#endif