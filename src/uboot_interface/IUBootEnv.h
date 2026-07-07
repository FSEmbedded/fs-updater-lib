#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace UBoot
{
    /**
     * Read/stage access to the U-Boot environment, as consumed by the
     * update state machine. Kept free of the libubootenv types so state
     * machine logic can run against an in-memory environment in tests.
     * Flushing and transaction scope stay on the concrete UBoot class:
     * the owner of the environment decides when staged values persist.
     */
    class IUBootEnv
    {
        public:
            virtual ~IUBootEnv() = default;

            IUBootEnv(const IUBootEnv &) = delete;
            IUBootEnv &operator=(const IUBootEnv &) = delete;
            IUBootEnv(IUBootEnv &&) = delete;
            IUBootEnv &operator=(IUBootEnv &&) = delete;

            /**
             * Stage a variable-value pair; persisted on the next flush.
             * @param key Variable name of the UBoot-Environment.
             * @param value Content for given variable.
             */
            virtual void addVariable(const std::string &key, const std::string &value) = 0;

            /**
             * Return variable content; must match the allowed list.
             * @throw UBootEnvVarNotAllowedContent
             * @throw UBootEnvVarCanNotConvertedIntoReturnType
             */
            virtual uint8_t getVariable(const std::string &variable_name, const std::vector<uint8_t> &allowed_list) = 0;
            virtual std::string getVariable(const std::string &variable_name, const std::vector<std::string> &allowed_list) = 0;
            virtual char getVariable(const std::string &variable_name, const std::vector<char> &allowed_list) = 0;

            /**
             * Return variable content validated by a predicate.
             * @throw UBootEnvVarNotAllowedContent When validator returns false.
             */
            virtual std::string getVariable(const std::string &variable_name, bool (*validator)(const std::string &)) = 0;

        protected:
            IUBootEnv() = default;
    };
}
