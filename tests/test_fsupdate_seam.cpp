#include <gtest/gtest.h>

#include "handle_update/fsupdate.h"
#include "handle_update/fs_exceptions.h"
#include "handle_update/updater_exceptions.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "support/fake_uboot_env.h"

#include <map>
#include <memory>
#include <string>

namespace
{

using test_support::FakeUBootEnv;

std::shared_ptr<logger::LoggerHandler> quiet_logger()
{
    static auto handler =
        logger::LoggerHandler::initLogger(std::make_shared<logger::LoggerSinkEmpty>(logger::logLevel::ERROR));
    return handler;
}

std::shared_ptr<FakeUBootEnv> env_with(const std::string &reboot_state)
{
    return std::make_shared<FakeUBootEnv>(std::map<std::string, std::string>{
        {"update_reboot_state", reboot_state},
        {"update", "0000"},
        {"rauc_cmd", "rauc.slot=A"},
        {"BOOT_A_LEFT", "3"},
        {"BOOT_B_LEFT", "3"},
        {"application", "A"},
    });
}

/* The routine mark-good. It is the only arm that puts the running slot's boot
 * budget back: while any update state is durable the boot-time gate withholds
 * the reset, so a slot that spent attempts and then settled would otherwise
 * stay one boot away from dropping out of the rotation. Reachable only through
 * the injected environment, and until now driven by nothing. */
TEST(FSUpdateSeam, CommitRestoresTheRunningSlotsBudgetWhenNothingIsPending)
{
    auto env = env_with("0");
    env->set("BOOT_A_LEFT", "1");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_TRUE(updater.commit_update());
    env->flushEnvironment();

    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    /* The slot that is not running is not touched: its budget belongs to the
     * bootloader's own accounting for the other side. */
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
    EXPECT_TRUE(env->writes_of("BOOT_B_LEFT").empty());
}

/* An intact budget must not be rewritten. A write here would be harmless in
 * value and wrong in kind: every boot would stage an environment change for a
 * device with nothing to settle. */
TEST(FSUpdateSeam, CommitWritesNothingWhenTheBudgetIsAlreadyWhole)
{
    auto env = env_with("0");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_FALSE(updater.commit_update());
    env->flushEnvironment();

    EXPECT_TRUE(env->writes_of("BOOT_A_LEFT").empty());
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
}

/* The commit door on a state this build cannot interpret. Reachable only
 * through the injected environment: before it existed, this outcome was
 * argued from reading the code and pinned by nothing. */
TEST(FSUpdateSeam, CommitRefusesAnUninterpretableRebootState)
{
    auto env = env_with("0x02");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_THROW((void)updater.commit_update(), fs::NotAllowedUpdateState);

    /* A refusal writes nothing: the state stays as it was found, so a later
     * build that can interpret it still sees the original content. */
    EXPECT_EQ(env->at("update_reboot_state"), "0x02");
    EXPECT_TRUE(env->writes_of("update_reboot_state").empty());
}

/* Apply names the state it cannot act on. The description has to be prose,
 * not a numeral: no numeral for the recovery state can exist in an
 * environment, so quoting one would invite reproducing it by hand -- which
 * is the write the no-persist rule forbids. */
TEST(FSUpdateSeam, ApplyRefusesAnUninterpretableRebootStateByDescription)
{
    auto env = env_with("012");
    fs::FSUpdate updater(env, quiet_logger());

    try
    {
        (void)updater.apply_pending_update();
        ADD_FAILURE() << "apply acted on a state it cannot interpret";
    }
    catch (const fs::ApplyUpdateInvalidState &e)
    {
        EXPECT_NE(std::string(e.what()).find("uninterpretable"), std::string::npos)
            << "the diagnostic was " << e.what();
        EXPECT_EQ(std::string(e.what()).find("13"), std::string::npos)
            << "the diagnostic quoted a numeral the environment cannot hold";
    }

    EXPECT_TRUE(env->nothing_staged());
}

/* Both rollbacks refuse before the first variable is staged. The verbs below
 * them are reachable through installed headers, so the refusal has to hold at
 * this layer too -- and a partial stage is worse than none. */
TEST(FSUpdateSeam, FirmwareRollbackRefusesUninterpretableStateWithoutStaging)
{
    auto env = env_with("abc");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_THROW(updater.rollback_firmware(), updater::RebootStateNotInterpretable);
    EXPECT_TRUE(env->nothing_staged());
}

TEST(FSUpdateSeam, ApplicationRollbackRefusesUninterpretableStateWithoutStaging)
{
    auto env = env_with("abc");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_THROW(updater.rollback_application(), updater::RebootStateNotInterpretable);
    EXPECT_TRUE(env->nothing_staged());
}

/* Non-vacuity for the two above: on a readable state the same calls may fail
 * for their own reasons, but never with the refusal that names the state
 * unreadable. Without this the refusals could be the shape being inert. */
TEST(FSUpdateSeam, RollbacksOnAReadableStateDoNotClaimItIsUnreadable)
{
    for (const char *verb : {"firmware", "application"})
    {
        SCOPED_TRACE(verb);
        auto env = env_with("0");
        fs::FSUpdate updater(env, quiet_logger());

        try
        {
            if (std::string(verb) == "firmware")
            {
                updater.rollback_firmware();
            }
            else
            {
                updater.rollback_application();
            }
        }
        catch (const updater::RebootStateNotInterpretable &)
        {
            ADD_FAILURE() << "a readable state was reported as not interpretable";
        }
        catch (const std::exception &)
        {
        }
    }
}

/* Positive control on the same shape. Without it the refusal above could be
 * the environment being inert rather than the state being rejected. */
TEST(FSUpdateSeam, CommitOnAReadableIdleStateDoesNotRefuse)
{
    auto env = env_with("0");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_NO_THROW((void)updater.commit_update());
}

}
