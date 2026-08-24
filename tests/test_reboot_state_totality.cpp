#include <gtest/gtest.h>

#include "handle_update/reboot_state.h"
#include "support/fake_uboot_env.h"
#include "handle_update/updateDefinitions.h"
#include "uboot_interface/IUBootEnv.h"
#include "uboot_interface/allowed_uboot_variable_states.h"
#include "uboot_interface/uboot_exceptions.h"

#include "logger/LoggerEntry.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerLevel.h"
#include "logger/LoggerSinkBase.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using update_definitions::UBootBootstateFlags;

/* Environment double for the seam only. The validator overload -- the one the
 * reader goes through -- serves raw content back unfiltered, so a test can
 * place bytes no allowed list would ever pass; the remaining overloads mirror
 * the concrete accessor's conversion and failure shapes, so a caller cannot
 * pass here on behaviour the real accessor does not have. It can also be told
 * to raise a chosen exception instead of answering -- all of it is what the
 * reader has to survive. */
class SeamEnv : public UBoot::IUBootEnv
{
  public:
    enum class Raise : unsigned char
    {
        NOTHING,
        ENV_ACCESS,
        CANNOT_CONVERT,
        NOT_ALLOWED_CONTENT,
        BAD_ALLOC
    };

    SeamEnv() = default;

    void seed(const std::string &key, const std::string &value)
    {
        env_[key] = value;
    }

    void erase(const std::string &key)
    {
        env_.erase(key);
    }

    void raise(Raise kind)
    {
        raise_ = kind;
    }

    void addVariable(const std::string &key, const std::string &value) override
    {
        staged_[key] = value;
    }

    void flushEnvironment() override
    {
        for (const auto &kv : staged_)
        {
            env_[kv.first] = kv.second;
        }
        staged_.clear();
    }

    /* Refcounted like the real accessor: a nested open does not reopen and
     * the matching close does not close early. */
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

    /* Numeric conversion, not the character code: content "2" is the number
     * two here as it is on a device. Failure modes are the accessor's own --
     * unconvertible or out of the target type raises
     * UBootEnvVarCanNotConvertedIntoReturnType, content outside the list
     * raises UBootEnvVarNotAllowedContent. Empty content reaches the first of
     * those rather than a std::out_of_range no accessor ever throws. */
    uint8_t getVariable(const std::string &name, const std::vector<uint8_t> &allowed) override
    {
        const std::string content = fetch(name);
        unsigned long number = 0UL;
        try
        {
            number = std::stoul(content);
        }
        catch (...)
        {
            throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType(
                name, "Variable content can not be converted into a unsigned long");
        }

        if (number > UCHAR_MAX)
        {
            throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType(name, "Variable fit not in type u_int8");
        }

        const auto value = static_cast<uint8_t>(number);
        if (std::find(allowed.cbegin(), allowed.cend(), value) == allowed.cend())
        {
            throw UBoot::UBootEnvVarNotAllowedContent(name, std::to_string(value), render(allowed));
        }
        return value;
    }

    std::string getVariable(const std::string &name, const std::vector<std::string> &allowed) override
    {
        const std::string content = fetch(name);
        if (std::find(allowed.cbegin(), allowed.cend(), content) == allowed.cend())
        {
            throw UBoot::UBootEnvVarNotAllowedContent(name, content, render(allowed));
        }
        return content;
    }

    char getVariable(const std::string &name, const std::vector<char> &allowed) override
    {
        const std::string content = fetch(name);
        if (content.length() != 1U)
        {
            throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType(name, "Variable fit not in type char");
        }

        const char value = content.at(0);
        if (std::find(allowed.cbegin(), allowed.cend(), value) == allowed.cend())
        {
            throw UBoot::UBootEnvVarNotAllowedContent(name, std::to_string(value), render(allowed));
        }
        return value;
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

    bool holds(const std::string &key) const
    {
        return env_.find(key) != env_.end();
    }

    const std::string &value_of(const std::string &key) const
    {
        return env_.at(key);
    }

  private:
    /* The accessor puts the allowed list into the message; only the exception
     * type and the failure mode carry meaning for a caller, the rendering is
     * cosmetic. */
    static std::string render(const std::vector<uint8_t> &allowed)
    {
        std::string out;
        for (const uint8_t elem : allowed)
        {
            out += std::to_string(elem) + std::string(" ");
        }
        return out;
    }

    static std::string render(const std::vector<std::string> &allowed)
    {
        std::string out;
        for (const std::string &elem : allowed)
        {
            out += elem + std::string(" | ");
        }
        return out;
    }

    static std::string render(const std::vector<char> &allowed)
    {
        std::string out;
        for (const char elem : allowed)
        {
            out += std::string(1, elem) + std::string(" | ");
        }
        return out;
    }

    std::string fetch(const std::string &name)
    {
        switch (raise_)
        {
        case Raise::ENV_ACCESS:
            throw UBoot::UBootEnvAccess(name);
        case Raise::CANNOT_CONVERT:
            throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType(name, "raw");
        case Raise::NOT_ALLOWED_CONTENT:
            throw UBoot::UBootEnvVarNotAllowedContent(name, "raw", "allowed list");
        case Raise::BAD_ALLOC:
            throw std::bad_alloc();
        case Raise::NOTHING:
        default:
            break;
        }
        const auto it = env_.find(name);
        if (it == env_.end())
        {
            throw UBoot::UBootEnvAccess(name);
        }
        return it->second;
    }

    std::map<std::string, std::string> env_;
    std::map<std::string, std::string> staged_;
    unsigned depth_ = 0U;
    Raise raise_ = Raise::NOTHING;
};

///////////////////////////////////////////////////////////////////////////////
/// The doubles themselves: they prove something only while they behave like
/// the accessor they stand in for. Run over every in-memory environment the
/// suites use, because a second double that drifts would turn error-path
/// tests green for the wrong reason.
///////////////////////////////////////////////////////////////////////////////

template <typename Env>
class SeamEnvFidelity : public ::testing::Test
{
};

using EnvDoubles = ::testing::Types<SeamEnv, test_support::FakeUBootEnv>;
TYPED_TEST_SUITE(SeamEnvFidelity, EnvDoubles);

TYPED_TEST(SeamEnvFidelity, TypedReadsConvertContentRatherThanReturningItsCharacterCode)
{
    TypeParam env;
    env.seed("update_reboot_state", "2");
    EXPECT_EQ(env.getVariable("update_reboot_state", std::vector<uint8_t>{0, 1, 2}), static_cast<uint8_t>(2));

    env.seed("application", "A");
    EXPECT_EQ(env.getVariable("application", std::vector<char>{'A', 'B'}), 'A');
}

TYPED_TEST(SeamEnvFidelity, EmptyContentFailsWithTheAccessorsOwnExceptionType)
{
    TypeParam env;
    env.seed("update_reboot_state", "");
    env.seed("application", "");

    /* A double that indexed the content directly would raise
     * std::out_of_range here -- a shape no accessor produces, so a caller that
     * survives only that would pass here and terminate on a device. */
    try
    {
        (void)env.getVariable("update_reboot_state", std::vector<uint8_t>{0, 1, 2});
        ADD_FAILURE() << "empty content answered instead of failing";
    }
    catch (const std::out_of_range &)
    {
        ADD_FAILURE() << "failed with a standard-library exception the accessor never raises";
    }
    catch (const UBoot::UBootEnvVarCanNotConvertedIntoReturnType &)
    {
    }

    try
    {
        (void)env.getVariable("application", std::vector<char>{'A', 'B'});
        ADD_FAILURE() << "empty content answered instead of failing";
    }
    catch (const std::out_of_range &)
    {
        ADD_FAILURE() << "failed with a standard-library exception the accessor never raises";
    }
    catch (const UBoot::UBootEnvVarCanNotConvertedIntoReturnType &)
    {
    }
}

/* The bracket is what holds the inter-process lock on a device, so a double
 * that let a nested close release it early would pass a test the device
 * fails. Balance is the property; the depth is only how it is observed. */
TYPED_TEST(SeamEnvFidelity, NestedTransactionsCloseOnlyWithTheOutermost)
{
    TypeParam env;
    EXPECT_EQ(env.env_open_depth(), 0U);
    {
        UBoot::EnvTransaction const outer(env);
        EXPECT_EQ(env.env_open_depth(), 1U);
        {
            UBoot::EnvTransaction const inner(env);
            EXPECT_EQ(env.env_open_depth(), 2U);
        }
        EXPECT_EQ(env.env_open_depth(), 1U) << "the inner scope closed the environment";
    }
    EXPECT_EQ(env.env_open_depth(), 0U);
}

TYPED_TEST(SeamEnvFidelity, ContentOutsideTheListIsToldApartFromContentThatDoesNotConvert)
{
    TypeParam env;

    env.seed("update_reboot_state", "9");
    EXPECT_THROW((void)env.getVariable("update_reboot_state", std::vector<uint8_t>{0, 1, 2}),
                 UBoot::UBootEnvVarNotAllowedContent);

    env.seed("update_reboot_state", "999");
    EXPECT_THROW((void)env.getVariable("update_reboot_state", std::vector<uint8_t>{0, 1, 2}),
                 UBoot::UBootEnvVarCanNotConvertedIntoReturnType);
}

///////////////////////////////////////////////////////////////////////////////
/// Totality of the decoder over the whole input space
///////////////////////////////////////////////////////////////////////////////

struct DecodeCase
{
    const char *name;
    std::string raw;
    UBootBootstateFlags expected;
};

/* One row per input. The canonical rows pin each numeral to its enumerator by
 * name, so narrowing the accepted set cannot pass unnoticed; every other row
 * pins that an input outside the alphabet becomes the recovery state instead
 * of a guess or a throw. */
std::vector<DecodeCase> decode_cases()
{
    return {
        /* canonical alphabet: every value a writer can emit */
        {"canonical_0", "0", UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING},
        {"canonical_1", "1", UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED},
        {"canonical_2", "2", UBootBootstateFlags::INCOMPLETE_FW_UPDATE},
        {"canonical_3", "3", UBootBootstateFlags::INCOMPLETE_APP_UPDATE},
        {"canonical_4", "4", UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE},
        {"canonical_5", "5", UBootBootstateFlags::FAILED_FW_UPDATE},
        {"canonical_6", "6", UBootBootstateFlags::FAILED_APP_UPDATE},
        {"canonical_7", "7", UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING},
        {"canonical_8", "8", UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING},
        {"canonical_9", "9", UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING},
        {"canonical_10", "10", UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK},
        {"canonical_11", "11", UBootBootstateFlags::INCOMPLETE_APP_ROLLBACK},
        {"canonical_12", "12", UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK},

        /* numerically out of range, including the sentinel's own numeral */
        {"out_of_range_13", "13", UBootBootstateFlags::UNKNOWN_STATE},
        {"out_of_range_14", "14", UBootBootstateFlags::UNKNOWN_STATE},
        {"out_of_range_99", "99", UBootBootstateFlags::UNKNOWN_STATE},
        {"out_of_range_255", "255", UBootBootstateFlags::UNKNOWN_STATE},
        {"out_of_range_256", "256", UBootBootstateFlags::UNKNOWN_STATE},
        {"out_of_range_u32_overflow", "4294967296", UBootBootstateFlags::UNKNOWN_STATE},
        {"out_of_range_beyond_u64", "184467440737095516160", UBootBootstateFlags::UNKNOWN_STATE},

        /* empty and whitespace, leading and trailing */
        {"empty", "", UBootBootstateFlags::UNKNOWN_STATE},
        {"space_only", " ", UBootBootstateFlags::UNKNOWN_STATE},
        {"tab_only", "\t", UBootBootstateFlags::UNKNOWN_STATE},
        {"newline_only", "\n", UBootBootstateFlags::UNKNOWN_STATE},
        {"trailing_space", "2 ", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_space", " 2", UBootBootstateFlags::UNKNOWN_STATE},

        /* not a decimal numeral at all */
        {"alpha", "abc", UBootBootstateFlags::UNKNOWN_STATE},
        {"digit_then_alpha", "2abc", UBootBootstateFlags::UNKNOWN_STATE},
        {"hex_prefix", "0x02", UBootBootstateFlags::UNKNOWN_STATE},
        {"explicit_plus", "+2", UBootBootstateFlags::UNKNOWN_STATE},
        {"negative", "-1", UBootBootstateFlags::UNKNOWN_STATE},
        {"decimal_point", "2.0", UBootBootstateFlags::UNKNOWN_STATE},
        {"decimal_comma", "2,0", UBootBootstateFlags::UNKNOWN_STATE},
        {"non_ascii_digit", "\xef\xbc\x90", UBootBootstateFlags::UNKNOWN_STATE},

        /* non-canonical decimal: in range as a number, but no writer emits it.
         * Treated as tampering evidence rather than as its numeric value --
         * a deliberate decision, pinned here so it cannot drift silently. */
        {"leading_zero_00", "00", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_01", "01", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_02", "02", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_03", "03", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_04", "04", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_05", "05", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_06", "06", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_07", "07", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_08", "08", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_09", "09", UBootBootstateFlags::UNKNOWN_STATE},
        {"leading_zero_012", "012", UBootBootstateFlags::UNKNOWN_STATE},

        /* embedded NUL: built with an explicit length, a literal would end at
         * the NUL and silently degrade this row into a canonical one */
        {"digit_then_nul", std::string("2\0", 2), UBootBootstateFlags::UNKNOWN_STATE},
        {"nul_only", std::string(1, '\0'), UBootBootstateFlags::UNKNOWN_STATE},
        {"nul_then_digit", std::string("\0" "2", 2), UBootBootstateFlags::UNKNOWN_STATE},

        /* a length no environment variable of this kind ever has */
        {"very_long_digits", std::string(4096, '7'), UBootBootstateFlags::UNKNOWN_STATE},
        {"very_long_text", std::string(65536, 'x'), UBootBootstateFlags::UNKNOWN_STATE},
    };
}

class RebootStateDecode : public ::testing::TestWithParam<DecodeCase>
{
};

TEST_P(RebootStateDecode, DecodesWithoutThrowing)
{
    const DecodeCase &row = GetParam();

    UBootBootstateFlags decoded = UBootBootstateFlags::UNKNOWN_STATE;
    EXPECT_NO_THROW(decoded = update_definitions::decode_update_reboot_state(row.raw));
    EXPECT_EQ(decoded, row.expected) << "input length " << row.raw.size();
}

INSTANTIATE_TEST_SUITE_P(Totality, RebootStateDecode, ::testing::ValuesIn(decode_cases()),
                         [](const ::testing::TestParamInfo<DecodeCase> &info) { return std::string(info.param.name); });

/* The alphabet is declared twice: as the allowed list and as the decoder's own
 * range check. Nothing in the library couples them, so this test is the only
 * thing that notices when one is narrowed and the other is not. */
TEST(RebootStateAlphabet, DecoderAcceptsExactlyTheAllowedList)
{
    for (unsigned value = 0U; value <= 255U; ++value)
    {
        const bool listed = std::find(allowed_update_reboot_state_variables.begin(),
                                      allowed_update_reboot_state_variables.end(),
                                      static_cast<uint8_t>(value)) != allowed_update_reboot_state_variables.end();
        const bool accepted =
            update_definitions::decode_update_reboot_state(std::to_string(value)) != UBootBootstateFlags::UNKNOWN_STATE;
        EXPECT_EQ(listed, accepted) << "value " << value;
    }
}

///////////////////////////////////////////////////////////////////////////////
/// Totality of the reader, one level up, across the environment seam
///////////////////////////////////////////////////////////////////////////////

TEST(RebootStateReader, AbsentVariableYieldsRecoveryState)
{
    SeamEnv env;
    env.erase("update_reboot_state");

    UBootBootstateFlags state = UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
    EXPECT_NO_THROW(state = update_definitions::read_update_reboot_state(env));
    EXPECT_EQ(state, UBootBootstateFlags::UNKNOWN_STATE);
}

TEST(RebootStateReader, GarbageContentYieldsRecoveryState)
{
    SeamEnv env;
    for (const std::string &raw : {std::string("13"), std::string("0x02"), std::string(""), std::string("012"),
                                   std::string(" 2"), std::string("2abc")})
    {
        env.seed("update_reboot_state", raw);
        UBootBootstateFlags state = UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
        EXPECT_NO_THROW(state = update_definitions::read_update_reboot_state(env));
        EXPECT_EQ(state, UBootBootstateFlags::UNKNOWN_STATE) << "raw: " << raw;
    }
}

TEST(RebootStateReader, CanonicalContentStillDecodes)
{
    SeamEnv env;
    env.seed("update_reboot_state", "12");

    UBootBootstateFlags state = UBootBootstateFlags::UNKNOWN_STATE;
    EXPECT_NO_THROW(state = update_definitions::read_update_reboot_state(env));
    EXPECT_EQ(state, UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK);
}

/* The exception types are the real accessor's, not stand-ins: a reader that
 * caught a narrower type would pass against a stand-in and terminate on a
 * device. */
class RebootStateReaderRaises : public ::testing::TestWithParam<SeamEnv::Raise>
{
};

TEST_P(RebootStateReaderRaises, AccessorFailureYieldsRecoveryState)
{
    SeamEnv env;
    env.seed("update_reboot_state", "2");
    env.raise(GetParam());

    UBootBootstateFlags state = UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
    EXPECT_NO_THROW(state = update_definitions::read_update_reboot_state(env));
    EXPECT_EQ(state, UBootBootstateFlags::UNKNOWN_STATE);
}

INSTANTIATE_TEST_SUITE_P(EveryAccessorFailure, RebootStateReaderRaises,
                         ::testing::Values(SeamEnv::Raise::ENV_ACCESS, SeamEnv::Raise::CANNOT_CONVERT,
                                           SeamEnv::Raise::NOT_ALLOWED_CONTENT, SeamEnv::Raise::BAD_ALLOC),
                         [](const ::testing::TestParamInfo<SeamEnv::Raise> &info) {
                             switch (info.param)
                             {
                             case SeamEnv::Raise::ENV_ACCESS:
                                 return std::string("UBootEnvAccess");
                             case SeamEnv::Raise::CANNOT_CONVERT:
                                 return std::string("UBootEnvVarCanNotConvertedIntoReturnType");
                             case SeamEnv::Raise::NOT_ALLOWED_CONTENT:
                                 return std::string("UBootEnvVarNotAllowedContent");
                             case SeamEnv::Raise::BAD_ALLOC:
                                 return std::string("OutsideTheUBootHierarchy");
                             case SeamEnv::Raise::NOTHING:
                             default:
                                 return std::string("Nothing");
                             }
                         });

/* Reading must not be a write in disguise: a repair-on-read would persist a
 * value the running image invented. */
TEST(RebootStateReader, ReadingNeverTouchesTheEnvironment)
{
    SeamEnv env;
    env.seed("update_reboot_state", "13");

    (void)update_definitions::read_update_reboot_state(env);
    env.flushEnvironment();

    EXPECT_EQ(env.value_of("update_reboot_state"), "13");
}

///////////////////////////////////////////////////////////////////////////////
/// No-Persist: the recovery state has no encoding, so it cannot be written
///////////////////////////////////////////////////////////////////////////////

/* The load-bearing mechanism. A device that stored an out-of-alphabet value
 * would make every read on an older image fail after a fallback onto it. */
TEST(NoPersist, RecoveryStateHasNoEncoding)
{
    EXPECT_THROW((void)update_definitions::to_string(UBootBootstateFlags::UNKNOWN_STATE), std::logic_error);
}

/* to_string() throws for the recovery state by design, and describe() is the
 * renderer that lets a diagnostic name that state anyway. It is used inside a
 * throw-expression -- the invalid-state exception is constructed from it, on a
 * path the total reader can reach -- so a describe() that delegated straight to
 * to_string() would replace the intended diagnostic with an unrelated
 * std::logic_error escaping the caller. Total for every enumerator, therefore,
 * and never the numeral the environment cannot hold. */
TEST(DescribeState, IsTotalAndNeverRendersTheRecoveryStateAsANumeral)
{
    for (unsigned value = 0U; value <= static_cast<unsigned>(UBootBootstateFlags::UNKNOWN_STATE); ++value)
    {
        const auto flag = static_cast<UBootBootstateFlags>(value);
        std::string rendered;
        EXPECT_NO_THROW(rendered = update_definitions::describe(flag)) << "state " << value;
        EXPECT_FALSE(rendered.empty()) << "state " << value;
    }

    /* Real states keep their canonical numeral, so a diagnostic stays
     * comparable with what the environment holds. */
    for (const uint8_t value : allowed_update_reboot_state_variables)
    {
        const auto flag = static_cast<UBootBootstateFlags>(value);
        EXPECT_EQ(update_definitions::describe(flag), update_definitions::to_string(flag));
    }

    const std::string recovery = update_definitions::describe(UBootBootstateFlags::UNKNOWN_STATE);
    EXPECT_NE(recovery, "13");
    EXPECT_EQ(update_definitions::decode_update_reboot_state(recovery), UBootBootstateFlags::UNKNOWN_STATE)
        << "renders as a value the alphabet contains: " << recovery;
}

/* Property over the whole writer surface: staging any state through the one
 * encode-then-stage step a writer performs must either refuse outright or
 * leave the environment holding a value the alphabet contains. */
class NoPersistWriterSurface : public ::testing::TestWithParam<unsigned>
{
};

TEST_P(NoPersistWriterSurface, StagingEitherRefusesOrStaysInsideTheAlphabet)
{
    const auto flag = static_cast<UBootBootstateFlags>(GetParam());

    SeamEnv env;
    bool refused = false;
    try
    {
        env.addVariable("update_reboot_state", update_definitions::to_string(flag));
        env.flushEnvironment();
    }
    catch (const std::logic_error &)
    {
        refused = true;
    }

    if (refused)
    {
        EXPECT_FALSE(env.holds("update_reboot_state"));
        return;
    }

    ASSERT_TRUE(env.holds("update_reboot_state"));
    const std::string &persisted = env.value_of("update_reboot_state");
    EXPECT_NE(persisted, "13");
    EXPECT_NE(update_definitions::decode_update_reboot_state(persisted), UBootBootstateFlags::UNKNOWN_STATE)
        << "persisted: " << persisted;
}

INSTANTIATE_TEST_SUITE_P(EveryEnumerator, NoPersistWriterSurface,
                         ::testing::Range(0U, static_cast<unsigned>(UBootBootstateFlags::UNKNOWN_STATE) + 1U),
                         [](const ::testing::TestParamInfo<unsigned> &info) {
                             return std::string("state_") + std::to_string(info.param);
                         });

///////////////////////////////////////////////////////////////////////////////
/// The diagnostic the reader promises to leave behind
///////////////////////////////////////////////////////////////////////////////

/* A recording endpoint. LoggerSinkBase is the documented extension point for
 * endpoints and LoggerSinkEmpty is the in-tree example, so observing entries
 * needs nothing the library does not already offer. */
class RecordingSink : public logger::LoggerSinkBase
{
  public:
    void setLogEntry(const std::shared_ptr<logger::LogEntry> &ptr) override
    {
        std::lock_guard<std::mutex> const lock(guard_);
        entries_.push_back(ptr);
    }

    std::vector<std::shared_ptr<logger::LogEntry>> entries() const
    {
        std::lock_guard<std::mutex> const lock(guard_);
        return entries_;
    }

  private:
    mutable std::mutex guard_;
    std::vector<std::shared_ptr<logger::LogEntry>> entries_;
};

/* Run one read with a recording endpoint attached and hand back what the sink
 * saw. The handler is constructed directly instead of through initLogger():
 * that one keeps every handler in a store that lives as long as the process,
 * so it would never destruct -- and destruction is what drains the queue and
 * joins the sink thread, which is what makes the entries observable at all. */
std::vector<std::shared_ptr<logger::LogEntry>> read_and_collect(SeamEnv &env, UBootBootstateFlags &state)
{
    auto sink = std::make_shared<RecordingSink>();
    {
        auto handler = std::make_shared<logger::LoggerHandler>(sink);
        state = update_definitions::read_update_reboot_state(env, handler);
    }
    return sink->entries();
}

bool any_error_entry_contains(const std::vector<std::shared_ptr<logger::LogEntry>> &entries,
                              const std::string &needle)
{
    return std::any_of(entries.cbegin(), entries.cend(), [&needle](const std::shared_ptr<logger::LogEntry> &entry) {
        return (entry->getLogLevel() == logger::logLevel::ERROR) &&
               (entry->getLogMessage().find(needle) != std::string::npos);
    });
}

class RebootStateReaderLogsRaw : public ::testing::TestWithParam<std::string>
{
};

/* The raw content is the single most useful fact for diagnosing a field
 * device: the decode is total, so without it the log says only that something
 * was wrong and never what the environment actually held. */
TEST_P(RebootStateReaderLogsRaw, UninterpretableContentIsReportedVerbatimAtErrorLevel)
{
    SeamEnv env;
    env.seed("update_reboot_state", GetParam());

    UBootBootstateFlags state = UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
    const auto entries = read_and_collect(env, state);

    ASSERT_EQ(state, UBootBootstateFlags::UNKNOWN_STATE);
    EXPECT_TRUE(any_error_entry_contains(entries, GetParam())) << "raw content missing from the log";
    EXPECT_TRUE(any_error_entry_contains(entries, "update_reboot_state")) << "log does not name the variable";
}

INSTANTIATE_TEST_SUITE_P(RawContentReachesTheLog, RebootStateReaderLogsRaw,
                         ::testing::Values(std::string("13"), std::string("0x02"), std::string("012"),
                                           std::string(" 2"), std::string("2abc"), std::string("abc")),
                         [](const ::testing::TestParamInfo<std::string> &info) {
                             std::string name;
                             for (const char c : info.param)
                             {
                                 name += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
                             }
                             return name;
                         });

/* Empty content has no bytes to quote, so the entry itself is the evidence. */
TEST(RebootStateReaderDiagnostics, EmptyContentIsStillReportedAtErrorLevel)
{
    SeamEnv env;
    env.seed("update_reboot_state", "");

    UBootBootstateFlags state = UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
    const auto entries = read_and_collect(env, state);

    ASSERT_EQ(state, UBootBootstateFlags::UNKNOWN_STATE);
    EXPECT_TRUE(any_error_entry_contains(entries, "update_reboot_state"));
}

/* A read that never got as far as content must say so too -- an absent or
 * unreadable variable is a different fault from a decodable one, and the
 * totality of the reader is exactly what would otherwise hide it. */
TEST(RebootStateReaderDiagnostics, ReadFailureIsReportedAtErrorLevel)
{
    SeamEnv env;
    env.erase("update_reboot_state");

    UBootBootstateFlags state = UBootBootstateFlags::NO_UPDATE_REBOOT_PENDING;
    const auto entries = read_and_collect(env, state);

    ASSERT_EQ(state, UBootBootstateFlags::UNKNOWN_STATE);
    EXPECT_TRUE(any_error_entry_contains(entries, "update_reboot_state"));
}

/* Every read of a healthy device goes through this reader, so a diagnostic
 * emitted on the ordinary path would be noise that hides the real one. */
TEST(RebootStateReaderDiagnostics, CanonicalContentIsReportedAsNoFailure)
{
    SeamEnv env;
    env.seed("update_reboot_state", "12");

    UBootBootstateFlags state = UBootBootstateFlags::UNKNOWN_STATE;
    const auto entries = read_and_collect(env, state);

    ASSERT_EQ(state, UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK);
    for (const auto &entry : entries)
    {
        EXPECT_NE(entry->getLogLevel(), logger::logLevel::ERROR) << entry->getLogMessage();
    }
}

} // namespace
