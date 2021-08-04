#ifndef UBOOT_INTERFACE_H
#define UBOOT_INTERFACE_H

extern "C" {
    #include <stdlib.h>
    #include <libuboot.h>
}

#include <string>
#include <exception>
#include <map>
#include <mutex>

#define UBOOT_CONFIG_PATH "/etc/fw_env.config"

namespace UBoot
{
    ///////////////////////////////////////////////////////////////////////////
    /// UBoot' exception definitions
    ///////////////////////////////////////////////////////////////////////////

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
            explicit UBootEnvAccess(const std::string &var_name)
            {
                this->error_string = std::string("Error while access U-Boot Env; variable: \"")
                                    + var_name + std::string("\"");

            }
    };

    class UBootEnvWrite : public UBootError
    {
        public:
            UBootEnvWrite(const std::string &var_name, const std::string &var_content)
            {
                this-> error_string = std::string("Error while writing in U-Boot Env; variable: \"") + var_name;
                this->error_string += std::string("\"; content:\"") + var_content + std::string("\"");
            }
    };

    class UBootEnv : public UBootError
    {
        public:
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
            explicit UBoot(const std::string &);
            ~UBoot();
            UBoot(const UBoot &) = delete;
            UBoot &operator=(const UBoot &) = delete;
            UBoot(UBoot &&) = delete;
            UBoot &operator=(UBoot &&) = delete;
            
            std::string getVariable(const std::string &);
            void addVariable(const std::string &key, const std::string &value);
            void freeVariables();
            void flushEnvironment();
    };
};

#endif