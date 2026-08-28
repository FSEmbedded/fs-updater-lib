/* The bitfield's invariant is worth exactly as many paths as check it.
 *
 * The per-bit validator used to run on the read path alone: an invalid field
 * could be written, and from then on every read of the variable raised --
 * including the reads the commit, rollback and install verbs need, which is
 * how a device could end up with no verb that works. Routing every write
 * through write_update_bits() is what makes the check unconditional, and a new
 * call site writing the variable directly would quietly take that back. The
 * source-walking cases here pin that none does, at both levels: the write
 * helper, and the digit idioms that keep a single digit meaningful.
 */
#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "support/fake_uboot_env.h"
#include "uboot_interface/allowed_uboot_variable_states.h"

namespace
{
    std::vector<uint8_t> bits(const std::string &value)
    {
        return std::vector<uint8_t>(value.begin(), value.end());
    }

    /* The one file that may name the variable in an addVariable call: it is
     * the helper every other site goes through. */
    const char *const HELPER_FILE = "allowed_uboot_variable_states.h";
}

TEST(UpdateBitsWrite, RefusesAFieldThatWouldRaiseOnEveryLaterRead)
{
    test_support::FakeUBootEnv env({{"update", "0000"}});

    /* Two uncommitted firmware slots: the shape a stale digit and an install's
     * own target digit produce together. */
    EXPECT_THROW(write_update_bits(env, bits("1010")), UBoot::UBootEnvVarNotAllowedContent);

    /* And the write really did not happen -- a refusal that still writes is
     * worse than no refusal at all. */
    EXPECT_TRUE(env.writes_of("update").empty());
}

TEST(UpdateBitsWrite, AcceptsAFieldTheReadPathWouldAccept)
{
    test_support::FakeUBootEnv env({{"update", "0000"}});

    EXPECT_NO_THROW(write_update_bits(env, bits("0010")));
    ASSERT_EQ(env.writes_of("update").size(), 1U);
    EXPECT_EQ(env.writes_of("update").front(), "0010");
}

TEST(UpdateBitsWrite, NoSourceFileWritesTheBitfieldDirectly)
{
    const std::filesystem::path root(FS_UPDATER_SRC_DIR);
    ASSERT_TRUE(std::filesystem::exists(root)) << "source root not found: " << root;

    std::vector<std::string> offenders;
    std::size_t scanned = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h")
        {
            continue;
        }
        if (entry.path().filename().string() == HELPER_FILE)
        {
            continue;
        }

        std::ifstream file(entry.path());
        std::ostringstream text;
        text << file.rdbuf();
        ++scanned;

        if (text.str().find("addVariable(\"update\"") != std::string::npos)
        {
            offenders.push_back(entry.path().string());
        }
    }

    /* Non-vacuity: a scan that walked nothing would pass by seeing nothing. */
    EXPECT_GT(scanned, 10U) << "the source scan found almost no files -- check FS_UPDATER_SRC_DIR";

    for (const std::string &offender : offenders)
    {
        ADD_FAILURE() << offender << " writes the bitfield directly; go through write_update_bits()";
    }
}

/* The same argument one level down. Routing every write through
 * write_update_bits() keeps the field valid; it does not keep a single digit
 * meaningful. A bare `update.at(i) = '0'` overwrites both facts the digit
 * carries, so a slot's verdict was erased whenever an update settled -- and
 * nothing said so, because the digit that came out was legal.
 *
 * The four idioms name which fact moves: settled clears the uncommitted bit,
 * marked_bad sets the verdict, their composition records a finished update on
 * a condemned slot, and reprovisioned is the one place a verdict is
 * deliberately forgotten. A new literal would silently take that back, so it
 * is refused here rather than found later on a device.
 */
TEST(UpdateBitsWrite, NoSourceFileWritesABareDigitIntoTheBitfield)
{
    const std::filesystem::path root(FS_UPDATER_SRC_DIR);
    ASSERT_TRUE(std::filesystem::exists(root)) << "source root not found: " << root;

    /* Looks for `<anything>.at(...) = '<digit>'` within one statement -- the
     * assignment form a direct digit write takes, with any receiver name, so
     * a copy called `update_bits` is caught as readily as one called `update`.
     * Spelled as a scan rather than a regex: src/ carries no std::regex and
     * the helpers exist to keep it that way, so a test is a poor place to
     * introduce the first one.
     *
     * What this does NOT see, stated rather than rounded up: a digit written
     * through a reference (`for (uint8_t &d : update) d = '0';`), through
     * `operator[]`, or computed into a temporary first. A `.at()` on some
     * other container would be a false positive -- which is a reviewer looking
     * at a line, not a defect slipping past. */
    const auto assigns_bare_digit = [](const std::string &text) {
        for (std::size_t at = text.find(".at("); at != std::string::npos;
             at = text.find(".at(", at + 1))
        {
            const std::size_t end = text.find(';', at);
            const std::string statement = text.substr(at, (end == std::string::npos) ? end : end - at);
            const std::size_t eq = statement.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            const std::size_t quote = statement.find('\'', eq);
            /* `= '<digit>'` -- a quote, one digit, a closing quote. */
            if (quote != std::string::npos && quote + 2 < statement.size() &&
                std::isdigit(static_cast<unsigned char>(statement[quote + 1])) != 0 && statement[quote + 2] == '\'')
            {
                return true;
            }
        }
        return false;
    };

    std::vector<std::string> offenders;
    std::size_t scanned = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h")
        {
            continue;
        }

        std::ifstream file(entry.path());
        std::ostringstream text;
        text << file.rdbuf();
        ++scanned;

        if (assigns_bare_digit(text.str()))
        {
            offenders.push_back(entry.path().string());
        }
    }

    EXPECT_GT(scanned, 10U) << "the source scan found almost no files -- check FS_UPDATER_SRC_DIR";

    for (const std::string &offender : offenders)
    {
        ADD_FAILURE() << offender
                      << " assigns a bare digit into the update field; use digit_settled(), "
                         "digit_marked_bad() or digit_reprovisioned() so the intent is on the page";
    }
}

/* Hardening: the idioms must not be able to manufacture a field the validator
 * refuses. A write that raises leaves the caller mid-verb on a device whose
 * every later read of the variable raises too -- the exact shape the write
 * helper exists to prevent, reachable again if an idiom can produce it.
 *
 * Swept over every one of the 256 field states and every index. Settling and
 * condemning can only clear or set the bad bit, so they can never add an
 * uncommitted slot; reprovisioning sets one deliberately, and is the only
 * operation that can push a field to two uncommitted slots of a kind -- which
 * is why the install path settles stale digits before it runs.
 */
TEST(UpdateBitsWrite, NoIdiomTurnsAValidFieldIntoOneTheValidatorRefuses)
{
    std::size_t swept = 0;
    std::size_t reprovision_collisions = 0;

    for (int a = 0; a < 4; ++a)
    {
        for (int b = 0; b < 4; ++b)
        {
            for (int c = 0; c < 4; ++c)
            {
                for (int d = 0; d < 4; ++d)
                {
                    std::string field;
                    field += static_cast<char>('0' + a);
                    field += static_cast<char>('0' + b);
                    field += static_cast<char>('0' + c);
                    field += static_cast<char>('0' + d);
                    if (!validate_update_bits(field))
                    {
                        continue; /* not a state the device can hold */
                    }
                    ++swept;

                    for (std::size_t i = 0; i < field.size(); ++i)
                    {
                        std::string settled = field;
                        settled[i] = static_cast<char>(digit_settled(static_cast<uint8_t>(field[i])));
                        EXPECT_TRUE(validate_update_bits(settled))
                            << "settling index " << i << " of " << field << " gave " << settled;

                        std::string condemned = field;
                        condemned[i] =
                            static_cast<char>(digit_marked_bad(static_cast<uint8_t>(field[i])));
                        EXPECT_TRUE(validate_update_bits(condemned))
                            << "condemning index " << i << " of " << field << " gave " << condemned;

                        std::string finished = field;
                        finished[i] = static_cast<char>(
                            digit_marked_bad(digit_settled(static_cast<uint8_t>(field[i]))));
                        EXPECT_TRUE(validate_update_bits(finished))
                            << "finishing index " << i << " of " << field << " gave " << finished;

                        std::string fresh = field;
                        fresh[i] = static_cast<char>(digit_reprovisioned());
                        if (!validate_update_bits(fresh))
                        {
                            /* The one permitted way to fail, and only that way:
                             * the other slot of the same kind was already in
                             * flight. */
                            const std::size_t sibling = (i < 2) ? (i + 2) : (i - 2);
                            EXPECT_TRUE(digit_in_flight(static_cast<uint8_t>(field[sibling])))
                                << "reprovisioning index " << i << " of " << field
                                << " gave the refused " << fresh << " with no in-flight sibling";
                            ++reprovision_collisions;
                        }
                    }
                }
            }
        }
    }

    /* Non-vacuity on both halves: the sweep really walked the field space, and
     * the collision branch really fired rather than being carried untested. */
    EXPECT_GT(swept, 100U) << "the sweep found almost no valid fields";
    EXPECT_GT(reprovision_collisions, 0U) << "the collision branch never ran; it asserts nothing";
}

/* The idioms' truth table, so the cases above rest on stated behaviour rather
 * than on three call sites agreeing with each other. */
TEST(UpdateBitsWrite, TheDigitIdiomsMoveExactlyOneFactEach)
{
    /* settled clears the uncommitted bit and nothing else: a verdict survives. */
    EXPECT_EQ(digit_settled('0'), '0');
    EXPECT_EQ(digit_settled('1'), '0');
    EXPECT_EQ(digit_settled('2'), '2');
    EXPECT_EQ(digit_settled('3'), '2');

    /* marked_bad sets the verdict and leaves "in flight" alone. */
    EXPECT_EQ(digit_marked_bad('0'), '2');
    EXPECT_EQ(digit_marked_bad('1'), '3');
    EXPECT_EQ(digit_marked_bad('2'), '2');
    EXPECT_EQ(digit_marked_bad('3'), '3');

    /* A finished update on a condemned slot: settle, then mark. This is what
     * the bare '2' happened to equal for every input, which is why converting
     * those call sites changed no behaviour. */
    for (uint8_t d = '0'; d <= '3'; ++d)
    {
        EXPECT_EQ(digit_marked_bad(digit_settled(d)), '2') << "input digit " << static_cast<char>(d);
    }

    /* Reprovisioning starts over, verdict included. */
    EXPECT_EQ(digit_reprovisioned(), '1');

    /* in_flight reads the bit, never the character. */
    EXPECT_TRUE(digit_in_flight('1'));
    EXPECT_TRUE(digit_in_flight('3'));
    EXPECT_FALSE(digit_in_flight('0'));
    EXPECT_FALSE(digit_in_flight('2'));
}
