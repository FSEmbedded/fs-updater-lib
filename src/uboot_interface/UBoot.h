#pragma once

extern "C" {
    #include <stdlib.h>
    #include <libuboot.h>
}

#include <string>
#include <exception>
#include <map>
#include <mutex>

#define UBOOT_CONFIG_PATH "/etc/fw_env.config"
/**
 * Class to abstract the libubootenv written in C.
 * Implement only a subset of the features combined with abbilities of C++.
 * Also throw exceptions when errors occur. Abstract so the "errno" variable.
 */
namespace UBoot
{
    ///////////////////////////////////////////////////////////////////////////
    /// UBoot' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Base class for exceptions with UBootEnvironment.
     */
    class UBootError : public std::exception
    {
        protected:
            std::string error_string;
        
        public:
            const char * what() const throw () 
            {
                return this->error_string.c_str();
            }
    };

    class UBootEnvAccess : public UBootError
    {
        public:
            /**
             *  Report error that occurs during the access of the UBoot-Environment.
             *  The variable is not stated in the UBoot-Environment.
             *  @param var_name Variable name which the error occurs
             */
            explicit UBootEnvAccess(const std::string &var_name)
            {
                this->error_string = std::string("Error while access U-Boot Env; variable: \"")
                                    + var_name + std::string("\"");

            }
    };

    class UBootEnvWrite : public UBootError
    {
        public:
            /**
             * Report error that occurs during the access of the UBoot-Environment.
             * Key-value parameters could not be written to the UBoot-Environment.
             * @param var_name Variable name which throws the error.
             * @param var_content The value of the variable.
             */
            UBootEnvWrite(const std::string &var_name, const std::string &var_content)
            {
                this-> error_string = std::string("Error while writing in U-Boot Env; variable: \"") + var_name;
                this->error_string += std::string("\"; content:\"") + var_content + std::string("\"");
            }
    };

    class UBootEnv : public UBootError
    {
        public:
            /**
             * Report error that occurs when a general problem of accessing
             * the UBoot-Environment happens.
             * @param error_string Contains the reason for the access problem.
             */
            explicit UBootEnv(const std::string &error_string)
            {
                this->error_string = std::string("Error during access U-Boot Env: ") + error_string;
            }
    };

    ///////////////////////////////////////////////////////////////////////////
    /// UBoot declaration
    ///////////////////////////////////////////////////////////////////////////

    class UBoot
    {
        private:
            struct uboot_ctx *ctx;
            std::map<std::string, std::string> variables;
            std::mutex guard;

        public:
            /**
             * Constructor of the UBoot-object.
             * Be careful with multiple objects to handle parallel access.
             * @param config_path Path to the fw_env.config file which sets the UBoot-Environment memory.
             */
            explicit UBoot(const std::string &);

            /**
             * Destructor close all open file objects of the libubootenv.
             */
            ~UBoot();

            UBoot(const UBoot &) = delete;
            UBoot &operator=(const UBoot &) = delete;
            UBoot(UBoot &&) = delete;
            UBoot &operator=(UBoot &&) = delete;
            
            /**
             * Return the current content for given variable.
             * The variable is always read from the UBoot-Environment.
             * @param variableName The variable of the UBoot-Environment variable.
             * @return Content of given variable.
             * @throw UBootEnv Error during access UBoot-Environment.
             * @throw UBootEnvAccess Error during attempt to read variable from UBoot-Environment.
             */
            std::string getVariable(const std::string &);

            /**
             * Add the variable-value pair to the internal memory of the object.
             * If the key is already set, the content wil be overwritten.
             * Nothing of the UBoot-Environment is not changed until the flushEnvironment function
             * is called.
             * @param key Variable name of the UBoot-Environment
             * @param value Content for given variable
             */
            void addVariable(const std::string &key, const std::string &value);

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
            void flushEnvironment();
    };
}