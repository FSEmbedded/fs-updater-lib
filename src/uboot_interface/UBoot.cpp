#include "UBoot.h"
#include <climits>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <optional>

extern "C"{
    #include <errno.h>
}

namespace {
// S1 exception-firewall scaffold: each UBoot method computes its result in an
// exception-free inner step and reports failure as a Fault; the public method
// re-raises the canonical UBoot exception at its boundary, so the engine's
// throwing contract stays byte-identical. UBoot.cpp therefore remains
// -fexceptions until its caller (the engine) is converted — see
// the coding standard §4 (the flag-flip lags the conversion).
enum class FaultKind { env, env_access, env_write, var_not_allowed, var_not_converted };

struct Fault {
    FaultKind kind;
    std::string a{};
    std::string b{};
    std::string c{};
};

[[noreturn]] void raise(const Fault & f)
{
    switch (f.kind)
    {
        case FaultKind::env:               throw UBoot::UBootEnv(f.a);
        case FaultKind::env_access:        throw UBoot::UBootEnvAccess(f.a);
        case FaultKind::env_write:         throw UBoot::UBootEnvWrite(f.a, f.b);
        case FaultKind::var_not_allowed:   throw UBoot::UBootEnvVarNotAllowedContent(f.a, f.b, f.c);
        case FaultKind::var_not_converted: throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType(f.a, f.b);
    }
    throw UBoot::UBootEnv("unreachable fault kind");
}
} // namespace

UBoot::UBoot::UBoot(const std::string & config_path)
    : ctx(nullptr), env_open_count_(0)
{
    if (::libuboot_initialize(&this->ctx, NULL) < 0)
    {
        throw(UBootEnv("Init libuboot failed"));
    }

    if (::libuboot_read_config(ctx, config_path.c_str()) < 0)
    {
	    ::libuboot_exit(this->ctx);
        this->ctx = nullptr;
        throw(UBootEnv("Reading fw_env.config failed"));
    }
}

UBoot::UBoot::~UBoot()
{
    if (this->env_open_count_ > 0)
    {
        ::libuboot_close(this->ctx);
    }
    if (this->ctx != nullptr)
    {
        ::libuboot_exit(this->ctx);
    }
}

void UBoot::UBoot::openEnv()
{
    Fault fault;
    const bool ok = [&]() -> bool {
        std::lock_guard<std::mutex> const lockGuard(this->guard);
        if (this->env_open_count_ > 0)
        {
            ++this->env_open_count_;
            return true;
        }
        if (::libuboot_open(this->ctx) < 0)
        {
            ::libuboot_close(this->ctx);
            fault = Fault{FaultKind::env, "Opening of Env failed"};
            return false;
        }
        this->env_open_count_ = 1;
        return true;
    }();
    if (!ok) { raise(fault); }
}

void UBoot::UBoot::closeEnv()
{
    std::lock_guard<std::mutex> const lockGuard(this->guard);
    if (this->env_open_count_ == 0)
    {
        return;
    }
    if (--this->env_open_count_ == 0)
    {
        ::libuboot_close(this->ctx);
        this->variables.clear();
    }
}

std::string UBoot::UBoot::getVariable(const std::string & variableName)
{
    Fault fault;
    const auto result = [&]() -> std::optional<std::string> {
        std::lock_guard<std::mutex> const lockGuard(this->guard);
        const bool caller_owns_env = (this->env_open_count_ > 0);

        if (!caller_owns_env)
        {
            if (::libuboot_open(this->ctx) < 0)
            {
                ::libuboot_close(this->ctx);
                fault = Fault{FaultKind::env, "Opening of Env failed"};
                return std::nullopt;
            }
        }

        char * ptr_var = ::libuboot_get_env(this->ctx, variableName.c_str());
        if (ptr_var == NULL)
        {
            if (!caller_owns_env)
            {
                ::libuboot_close(this->ctx);
            }
            fault = Fault{FaultKind::env_access, variableName};
            return std::nullopt;
        }

        // libuboot_get_env hands us a malloc'd C string; own it with a deleter so
        // it frees on every exit path.
        const std::unique_ptr<char, decltype(&std::free)> owned(ptr_var, std::free);
        std::string returnValue(ptr_var);

        if (!caller_owns_env)
        {
            ::libuboot_close(this->ctx);
        }

        return returnValue;
    }();
    if (!result) { raise(fault); }
    return *result;
}

void UBoot::UBoot::addVariable(const std::string & key, const std::string & value)
{
    std::lock_guard<std::mutex> const lockGuard(this->guard);
    this->variables[key] = value;
}

void UBoot::UBoot::freeVariables()
{
    std::lock_guard<std::mutex> const lockGuard(this->guard);
    this->variables.clear();
}

void UBoot::UBoot::flushEnvironment()
{
    Fault fault;
    const bool ok = [&]() -> bool {
        std::lock_guard<std::mutex> const lockGuard(this->guard);
        const bool caller_owns_env = (this->env_open_count_ > 0);

        if (!caller_owns_env)
        {
            if (::libuboot_open(this->ctx) < 0)
            {
                ::libuboot_close(this->ctx);
                fault = Fault{FaultKind::env, "Opening of Env failed"};
                return false;
            }
        }

        for (const auto & entry: this->variables)
        {
            const int status_libuboot = ::libuboot_set_env(this->ctx, entry.first.c_str(), entry.second.c_str());
            if (status_libuboot != 0)
            {
                if (!caller_owns_env)
                {
                    ::libuboot_close(this->ctx);
                }
                fault = Fault{FaultKind::env_write, entry.first, entry.second};
                return false;
            }
        }
        const int status_env_store = ::libuboot_env_store(this->ctx);
        if (status_env_store != 0)
        {
            if (!caller_owns_env)
            {
                ::libuboot_close(this->ctx);
            }
            fault = Fault{FaultKind::env, "Cannot write U-Boot Env"};
            return false;
        }

        this->variables.clear();

        if (!caller_owns_env)
        {
            ::libuboot_close(this->ctx);
        }
        return true;
    }();
    if (!ok) { raise(fault); }
}

uint8_t UBoot::UBoot::getVariable(const std::string &variable_name, const std::vector<uint8_t> &allowed_list)
{
    Fault fault;
    const auto result = [&]() -> std::optional<uint8_t> {
        const std::string content = this->getVariable(variable_name);
        unsigned long number;
        try
        {
            number = std::stoul(content);
        }
        catch(...)
        {
            fault = Fault{FaultKind::var_not_converted, variable_name, "Variable content can not be converted into a unsigned long"};
            return std::nullopt;
        }

        uint8_t return_value;
        if(number <= UCHAR_MAX)
        {
            return_value = static_cast<uint8_t>(number);
        }
        else
        {
            fault = Fault{FaultKind::var_not_converted, variable_name, "Variable fit not in type u_int8"};
            return std::nullopt;
        }

        if(std::find(allowed_list.begin(), allowed_list.end(), return_value) == allowed_list.end())
        {
            std::string allowed_list_ser;
            for(const auto & elem : allowed_list)
            {
                allowed_list_ser += std::to_string(elem) + std::string(" ");
            }

            fault = Fault{FaultKind::var_not_allowed, variable_name, std::to_string(return_value), allowed_list_ser};
            return std::nullopt;
        }

        return return_value;
    }();
    if (!result) { raise(fault); }
    return *result;
}

std::string UBoot::UBoot::getVariable(const std::string &variable_name, const std::vector<std::string> &allowed_list)
{
    Fault fault;
    const auto result = [&]() -> std::optional<std::string> {
        const std::string return_value = this->getVariable(variable_name);
        if(std::find(allowed_list.begin(), allowed_list.end(), return_value) == allowed_list.end())
        {
            std::string allowed_list_ser;
            for(const auto & elem : allowed_list)
            {
                allowed_list_ser += elem + std::string(" | ");
            }

            fault = Fault{FaultKind::var_not_allowed, variable_name, return_value, allowed_list_ser};
            return std::nullopt;
        }
        return return_value;
    }();
    if (!result) { raise(fault); }
    return *result;
}

std::string UBoot::UBoot::getVariable(const std::string &variable_name, bool (*validator)(const std::string &))
{
    Fault fault;
    const auto result = [&]() -> std::optional<std::string> {
        const std::string value = this->getVariable(variable_name);
        if (!validator(value)) {
            fault = Fault{FaultKind::var_not_allowed, variable_name, value, "per-bit validation"};
            return std::nullopt;
        }
        return value;
    }();
    if (!result) { raise(fault); }
    return *result;
}

char UBoot::UBoot::getVariable(const std::string &variable_name, const std::vector<char> &allowed_list)
{
    Fault fault;
    const auto result = [&]() -> std::optional<char> {
        const std::string content = this->getVariable(variable_name);

        if(content.length() != 1)
        {
            fault = Fault{FaultKind::var_not_converted, variable_name, "Variable fit not in type char"};
            return std::nullopt;
        }

        const char return_value = content.at(0);

        if(std::find(allowed_list.begin(), allowed_list.end(), return_value) == allowed_list.end())
        {
            std::string allowed_list_ser;
            for(const auto & elem : allowed_list)
            {
                allowed_list_ser += elem + std::string(" | ");
            }

            fault = Fault{FaultKind::var_not_allowed, variable_name, std::to_string(return_value), allowed_list_ser};
            return std::nullopt;
        }

        return return_value;
    }();
    if (!result) { raise(fault); }
    return *result;
}
