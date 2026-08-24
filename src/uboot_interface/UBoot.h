#pragma once

extern "C" {
    #include <stdlib.h>
    #include <libuboot.h>
}

#include "uboot_exceptions.h"
#include "IUBootEnv.h"

#include <string>
#include <exception>
#include <map>
#include <mutex>
#include <vector>

#ifndef UBOOT_CONFIG_PATH
#define UBOOT_CONFIG_PATH "/etc/fw_env.config"
#endif

/**
 * Class to abstract the libubootenv written in C.
 * Implement only a subset of the features combined with abbilities of C++.
 * Also throw exceptions when errors occur. Abstract so the "errno" variable.
 */
namespace UBoot
{
    ///////////////////////////////////////////////////////////////////////////
    /// UBoot declaration
    ///////////////////////////////////////////////////////////////////////////

    class UBoot : public IUBootEnv
    {
        private:
            struct uboot_ctx *ctx;
            std::map<std::string, std::string> variables;
            std::mutex guard;
            unsigned int env_open_count_;

        public:
            /**
             * Constructor of the UBoot-object.
             * Be careful with multiple objects to handle parallel access.
             * @param config_path Path to the fw_env.config file which sets the UBoot-Environment memory.
             */
            explicit UBoot(const std::string & /*config_path*/);

            /**
             * Open U-Boot environment and acquire inter-process file lock.
             * While open, getVariable() and flushEnvironment() reuse the open context
             * instead of opening/closing individually. This makes read-modify-write
             * sequences atomic with respect to other processes.
             * @throw UBootEnv If the environment cannot be opened.
             */
            void openEnv() override;

            /**
             * Close U-Boot environment and release inter-process file lock.
             * Safe to call when environment is not open (no-op).
             */
            void closeEnv() noexcept override;

            
            /**
             * Destructor close all open file objects of the libubootenv.
             */
            ~UBoot();

            UBoot(const UBoot &) = delete;
            UBoot &operator=(const UBoot &) = delete;
            UBoot(UBoot &&) = delete;
            UBoot &operator=(UBoot &&) = delete;
            
            /**
             * Add the variable-value pair to the internal memory of the object.
             * If the key is already set, the content wil be overwritten.
             * Nothing of the UBoot-Environment is not changed until the flushEnvironment function
             * is called.
             * @param key Variable name of the UBoot-Environment
             * @param value Content for given variable
             */
            void addVariable(const std::string &key, const std::string &value) override;

            /**
             * Remove all variables of the internal object buffer.
             */
            void freeVariables();

            /**
             * Flush all variable-value pairs from the internal memory to the UBoot-Environment.
             * Afterwards the internal memory will be freed.
             * @throw UBootEnv General access problem.
             * @throw UBootEnvWrite Error during write process on UBoot-Environment.
             */
            void flushEnvironment() override;

            /**
             * Return the current content for given variable.
             * The variable is always read from the UBoot-Environment.
             * @param variableName The variable of the UBoot-Environment variable.
             * @return Content of given variable.
             * @throw UBootEnv Error during access UBoot-Environment.
             * @throw UBootEnvAccess Error during attempt to read variable from UBoot-Environment.
             */
            std::string getVariable(const std::string & /*variableName*/);
            
            /**
             * Return variable from UBoot-Environment. Must match to type and given allowed list of content.
             * @param variableName Variable that should be read from UBoot-Environment.
             * @param allowed_list of allowed states inside the uboot variable.
             * @return Variable content in uint8 container.
             * @throw UBootEnvVarCanNotConvertedIntoReturnType
             * @throw UBootEnvVarNotAllowedContent
             */
            uint8_t getVariable(const std::string & /*variable_name*/, const std::vector<uint8_t> & /*allowed_list*/) override;
            /**
             * Return variable from UBoot-Environment. Must match to type and given allowed list of content.
             * @param variableName Variable that should be read from UBoot-Environment.
             * @param allowed_list of allowed states inside the uboot variable.
             * @return Variable content in string container.
             * @throw UBootEnvVarCanNotConvertedIntoReturnType
             * @throw UBootEnvVarNotAllowedContent
             */
            std::string getVariable(const std::string & /*variable_name*/, const std::vector<std::string> & /*allowed_list*/) override;
            /**
             * Return variable from UBoot-Environment. Must match to type and given allowed list of content.
             * @param variableName Variable that should be read from UBoot-Environment.
             * @param allowed_list of allowed states inside the uboot variable.
             * @return Variable content in char container.
             * @throw UBootEnvVarCanNotConvertedIntoReturnType
             * @throw UBootEnvVarNotAllowedContent
             */
            char getVariable(const std::string & /*variable_name*/, const std::vector<char> & /*allowed_list*/) override;
            /**
             * Return variable from UBoot-Environment validated by a predicate.
             * @param variable_name Variable to read.
             * @param validator Function returning true when the value is acceptable.
             * @return Variable content as string.
             * @throw UBootEnvVarNotAllowedContent When validator returns false.
             */
            std::string getVariable(const std::string &variable_name, bool (*validator)(const std::string &)) override;
    };
}
