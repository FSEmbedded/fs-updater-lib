/* The bitfield's invariant is worth exactly as many paths as check it.
 *
 * The per-bit validator used to run on the read path alone: an invalid field
 * could be written, and from then on every read of the variable raised --
 * including the reads the commit, rollback and install verbs need, which is
 * how a device could end up with no verb that works. Routing every write
 * through write_update_bits() is what makes the check unconditional, and a new
 * call site writing the variable directly would quietly take that back. The
 * second case here walks the shipped sources and pins that none does.
 */
#include <gtest/gtest.h>

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
