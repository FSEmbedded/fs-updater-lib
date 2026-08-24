#include <gtest/gtest.h>

#include "handle_update/fsupdate.h"
#include "handle_update/fs_exceptions.h"
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

/* Positive control on the same shape. Without it the refusal above could be
 * the environment being inert rather than the state being rejected. */
TEST(FSUpdateSeam, CommitOnAReadableIdleStateDoesNotRefuse)
{
    auto env = env_with("0");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_NO_THROW((void)updater.commit_update());
}

}
