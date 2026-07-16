#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace UBoot
{
    /**
     * Read/stage/flush access to the U-Boot environment, as consumed by the
     * update state machine. Kept free of the libubootenv types so state
     * machine logic can run against an in-memory environment in tests.
     * Transaction scope (open/close around a batch of flushes) stays on the
     * concrete UBoot class; only durable persistence of staged values is
     * part of this interface.
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

            /**
             * Persist staged variables. Callers that never write (Bootstate
             * reads) never need this; callers that stage writes
             * (applicationUpdate) must be able to flush them.
             */
            virtual void flushEnvironment() = 0;

        protected:
            IUBootEnv() = default;
    };
}
