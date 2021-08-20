#include "UBoot.h"
#include <climits>
#include <cstdlib>
#include <algorithm>

extern "C"{
    #include <errno.h>
}
UBoot::UBoot::UBoot(const std::string & config_path)
{   
    this->ctx = nullptr;
    if (libuboot_initialize(&this->ctx, NULL) < 0)
    {
        throw(UBootEnv("Init libuboot failed"));
    }

    if (libuboot_read_config(ctx, config_path.c_str()) < 0)
    {
	    libuboot_exit(this->ctx);
        this->ctx = nullptr;
        throw(UBootEnv("Reading fw_env.config failed"));
    }
    
}

UBoot::UBoot::~UBoot()
{
    if(this->ctx != nullptr)
    {
        libuboot_exit(this->ctx);
    }
}

std::string UBoot::UBoot::getVariable(const std::string & variableName)
{
    std::lock_guard<std::mutex> lockGuard(this->guard);
    if (libuboot_open(this->ctx) < 0)
    {
	    libuboot_exit(this->ctx);
        this->ctx = nullptr;
        throw(UBootEnv("Opening of Env failed"));
    }

    const char *ptr_var = libuboot_get_env(this->ctx, variableName.c_str());
    if (ptr_var == NULL)
    {
	    libuboot_exit(this->ctx);
        this->ctx = nullptr;
        throw(UBootEnvAccess(variableName));
    }

    std::string returnValue(ptr_var);
    free( (void*) ptr_var);
	libuboot_close(ctx);
    return returnValue;
}

void UBoot::UBoot::addVariable(const std::string & key, const std::string & value)
{
    std::lock_guard<std::mutex> lockGuard(this->guard);
    this->variables[key] = value;
}

void UBoot::UBoot::freeVariables()
{
    std::lock_guard<std::mutex> lockGuard(this->guard);
    this->variables.clear();
}

void UBoot::UBoot::flushEnvironment()
{   
    std::lock_guard<std::mutex> lockGuard(this->guard);
    if (libuboot_open(this->ctx) < 0)
    {
	    libuboot_exit(this->ctx);
        this->ctx = nullptr;
        throw(UBootEnv("Opening of Env failed"));
    }

    for (const auto entry: this->variables)
    {
        const int status_libuboot = libuboot_set_env(this->ctx, entry.first.c_str(), entry.second.c_str());
        if (status_libuboot != 0)
        {
            libuboot_exit(this->ctx);
            this->ctx = nullptr;
            throw(UBootEnvWrite(entry.first, entry.second));
        }
    }
    this->variables.clear();

    const int status_env_store = libuboot_env_store(this->ctx);
    if (status_env_store != 0)
    {
        libuboot_exit(this->ctx);
        this->ctx = nullptr;
        throw(UBootEnv("Cannot write U-Boot Env"));
    }

    libuboot_close(this->ctx);
}

uint8_t UBoot::UBoot::getVariable(const std::string &variable_name, const std::vector<uint8_t> &allowed_list)
{
    const std::string content = this->getVariable(variable_name);
    unsigned long number;
    try
    {
        number = std::stoul(content);
    }
    catch(...)
    {
        throw(UBootEnvVarCanNotConvertedIntoReturnType(variable_name, "Variable content can not be converted into a unsigned long"));
    }

    uint8_t return_value;
    if(number <= UCHAR_MAX)
    {
        return_value = uint8_t(number);
    }
    else
    {
        throw(UBootEnvVarCanNotConvertedIntoReturnType(variable_name, "Variable fit not in type u_int8"));
    }

    if(std::find(allowed_list.begin(), allowed_list.end(), return_value) == allowed_list.end())
    {
        std::string allowed_list_ser;
        for(const auto & elem : allowed_list)
        {
            allowed_list_ser += std::to_string(elem) + std::string(" ");
        }

        throw(UBootEnvVarNotAllowedContent(variable_name, std::to_string(return_value), allowed_list_ser));
    }

    return return_value;
}

std::string UBoot::UBoot::getVariable(const std::string &variable_name, const std::vector<std::string> &allowed_list)
{
    const std::string return_value = this->getVariable(variable_name);
    if(std::find(allowed_list.begin(), allowed_list.end(), return_value) == allowed_list.end())
    {
        std::string allowed_list_ser;
        for(const auto & elem : allowed_list)
        {
            allowed_list_ser += elem + std::string(" ");
        }

        throw(UBootEnvVarNotAllowedContent(variable_name, return_value, allowed_list_ser));
    }
    return return_value;
}

char UBoot::UBoot::getVariable(const std::string &variable_name, const std::vector<char> &allowed_list)
{
    const std::string content = this->getVariable(variable_name);

    if(content.length() != 1)
    {
        throw(UBootEnvVarCanNotConvertedIntoReturnType(variable_name, "Variable fit not in type char"));
    }

    const char return_value = content.at(0);

    if(std::find(allowed_list.begin(), allowed_list.end(), return_value) == allowed_list.end())
    {
        std::string allowed_list_ser;
        for(const auto & elem : allowed_list)
        {
            allowed_list_ser += elem + std::string(" ");
        }

        throw(UBootEnvVarNotAllowedContent(variable_name, std::to_string(return_value), allowed_list_ser));
    }

    return return_value;
}
