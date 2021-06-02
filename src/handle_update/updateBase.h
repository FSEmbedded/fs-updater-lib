#ifndef UPDATE_BASE_H
#define UPDATE_BASE_H

#include "updateDefinitions.h"
#include "../uboot_interface/UBoot.h"


#include <filesystem>
#include <memory>
#include <exception>
#include <string>

namespace updater
{
    ///////////////////////////////////////////////////////////////////////////
    /// updateBase' exception definitions
    ///////////////////////////////////////////////////////////////////////////
    class ErrorUpdateBaseException : public std::exception
    {
        protected:
            std::string error_msg;

        public:
            const char * what() const throw () 
            {
                return this->error_msg.c_str();
            }
    };

    ///////////////////////////////////////////////////////////////////////////
    /// firmwareUpdate declaration
    ///////////////////////////////////////////////////////////////////////////
    class updateBase
    {
        protected:
            std::shared_ptr<UBoot::UBoot> uboot_handler;

        public:
            explicit updateBase(const std::shared_ptr<UBoot::UBoot> &);
            ~updateBase();
            
            updateBase(const updateBase &) = delete;
            updateBase &operator=(const updateBase &) = delete;
            updateBase(updateBase &&) = delete;
            updateBase &operator=(updateBase &&) = delete;

            virtual void install(const std::filesystem::path &) = 0;
            update_definitions::UBootBootstateFlags status();
            virtual void rollback() = 0;
            virtual unsigned int getCurrentVersion() = 0;
    };
};

#endif