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
     * Transaction scope is part of the interface so callers can bracket
     * writes against any implementation.
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

            /**
             * Open the environment for a batch of writes. Bracketed by
             * closeEnv(); prefer EnvTransaction over calling either directly.
             */
            virtual void openEnv() = 0;

            /**
             * Close the environment opened by openEnv(). Runs from the
             * destructor of EnvTransaction, so it must not throw: an
             * exception leaving it during stack unwinding terminates the
             * process. Enforced here rather than stated, so a future
             * implementation cannot weaken it.
             */
            virtual void closeEnv() noexcept = 0;

        protected:
            IUBootEnv() = default;
    };

    /**
     * RAII bracket around a batch of environment writes. Takes the interface,
     * so the same scope works against a device and against an in-memory
     * environment.
     */
    class EnvTransaction
    {
        IUBootEnv &uboot_;

        public:
            explicit EnvTransaction(IUBootEnv &uboot) : uboot_(uboot) { uboot_.openEnv(); }
            ~EnvTransaction() { uboot_.closeEnv(); }
            EnvTransaction(const EnvTransaction &) = delete;
            EnvTransaction &operator=(const EnvTransaction &) = delete;
            EnvTransaction(EnvTransaction &&) = delete;
            EnvTransaction &operator=(EnvTransaction &&) = delete;
    };
}
