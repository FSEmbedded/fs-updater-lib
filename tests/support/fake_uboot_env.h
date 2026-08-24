#pragma once

#include "uboot_interface/IUBootEnv.h"
#include "uboot_interface/uboot_exceptions.h"

#include <climits>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test_support
{
    /* In-memory U-Boot environment, shared by every suite that needs one.
     * Mirrors the concrete accessor closely enough that error-path behaviour
     * cannot pass here and differ on a device: unknown keys and values outside
     * the allowed list raise the same exception shapes, and a value that does
     * not convert is told apart from one that is merely not in the list.
     * Deferred-write: addVariable() stages into a pending map, invisible to
     * getVariable() until flushEnvironment() — matching real UBoot::UBoot,
     * where a read never sees an unflushed write. This matters for
     * order-sensitive rollback logic that re-reads a variable it just staged.
     *
     * One definition, because the fidelity suite pins one definition; a second
     * copy could drift and turn error-path tests green for the wrong reason. */
    class FakeUBootEnv : public UBoot::IUBootEnv
    {
      public:
        FakeUBootEnv() = default;
        explicit FakeUBootEnv(std::map<std::string, std::string> seed) : env_(std::move(seed)) {}

        void addVariable(const std::string &key, const std::string &value) override
        {
            staged_[key] = value;
            journal_[key].push_back(value);
        }

        void flushEnvironment() override
        {
            for (auto &kv : staged_)
            {
                env_[kv.first] = kv.second;
            }
            staged_.clear();
        }

        uint8_t getVariable(const std::string &name, const std::vector<uint8_t> &allowed) override
        {
            const std::string raw = fetch(name);
            unsigned long number = 0UL;
            try
            {
                number = std::stoul(raw);
            }
            catch (const std::exception &)
            {
                throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType(name, raw);
            }
            /* Not-a-number and does-not-fit are one failure mode in the real
             * accessor and a different one from not-in-the-list; collapsing them
             * would let a caller distinguish here what it cannot distinguish on a
             * device. */
            if (number > UCHAR_MAX)
            {
                throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType(name, raw);
            }
            for (const uint8_t candidate : allowed)
            {
                if (candidate == static_cast<uint8_t>(number))
                {
                    return candidate;
                }
            }
            throw UBoot::UBootEnvVarNotAllowedContent(name, raw, "allowed list");
        }

        std::string getVariable(const std::string &name, const std::vector<std::string> &allowed) override
        {
            const std::string raw = fetch(name);
            for (const std::string &candidate : allowed)
            {
                if (candidate == raw)
                {
                    return raw;
                }
            }
            throw UBoot::UBootEnvVarNotAllowedContent(name, raw, "allowed list");
        }

        char getVariable(const std::string &name, const std::vector<char> &allowed) override
        {
            const std::string raw = fetch(name);
            if (raw.size() != 1)
            {
                throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType(name, raw);
            }
            for (const char candidate : allowed)
            {
                if (candidate == raw.front())
                {
                    return candidate;
                }
            }
            throw UBoot::UBootEnvVarNotAllowedContent(name, raw, "allowed list");
        }

        std::string getVariable(const std::string &name, bool (*validator)(const std::string &)) override
        {
            const std::string raw = fetch(name);
            if (!validator(raw))
            {
                throw UBoot::UBootEnvVarNotAllowedContent(name, raw, "validator");
            }
            return raw;
        }

        /* The bracket is refcounted like the real accessor: a nested open
         * does not reopen and the matching close does not close early. A
         * double that treated it as a no-op would let an unbalanced bracket
         * pass here and hold an inter-process lock open on a device. */
        void openEnv() override
        {
            ++depth_;
        }

        void closeEnv() noexcept override
        {
            if (depth_ > 0U)
            {
                --depth_;
            }
        }

        unsigned env_open_depth() const
        {
            return depth_;
        }

        const std::string &at(const std::string &name) const
        {
            return env_.at(name);
        }

        /* Seed an arbitrary raw value, bypassing the staged-write path. */
        void set(const std::string &name, const std::string &raw)
        {
            env_[name] = raw;
        }

        /* Same as set(), under the name the fidelity suite drives every double by. */
        void seed(const std::string &name, const std::string &raw)
        {
            set(name, raw);
        }

        /* Simulate a variable absent from the environment. */
        void unset(const std::string &name)
        {
            env_.erase(name);
            staged_.erase(name);
        }

        bool holds(const std::string &name) const
        {
            return env_.find(name) != env_.end();
        }

        /* Every value a verb ever staged for a variable, in order. A verb that
         * refuses after staging is still a verb that wrote, so the surviving
         * value alone cannot answer what was attempted. */
        std::vector<std::string> writes_of(const std::string &name) const
        {
            const auto it = journal_.find(name);
            return (it == journal_.end()) ? std::vector<std::string>() : it->second;
        }

        /* Whether anything at all was staged, for any variable. Staging a value
         * that is already there leaves the environment identical, so only the
         * journal can answer "nothing was attempted". */
        bool nothing_staged() const
        {
            return journal_.empty();
        }

      private:
        std::string fetch(const std::string &name) const
        {
            const auto it = env_.find(name);
            if (it == env_.end())
            {
                throw UBoot::UBootEnvAccess(name);
            }
            return it->second;
        }

        std::map<std::string, std::string> env_;
        std::map<std::string, std::string> staged_;
        std::map<std::string, std::vector<std::string>> journal_;
        unsigned depth_ = 0U;
    };
}
