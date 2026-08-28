#include <gtest/gtest.h>

#include "handle_update/fsupdate.h"
#include "handle_update/fs_exceptions.h"
#include "handle_update/updater_exceptions.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "support/fake_uboot_env.h"
#include "uboot_interface/allowed_uboot_variable_states.h"

#include <cerrno>
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

/* The terminal refusal has to name the state it refused. Reaching it means no
 * arm claimed the value -- either no verb owns that state, or the one that does
 * found its bitfield precondition false -- and the two are indistinguishable to
 * a caller that is told only that something was not allowed. Staged here as the
 * second case: state 3 with an untouched bitfield, so the arm that owns the
 * state is skipped and control falls through. */
TEST(FSUpdateSeam, CommitNamesTheStateItRefuses)
{
    auto env = env_with("3");
    fs::FSUpdate updater(env, quiet_logger());

    try
    {
        (void)updater.commit_update();
        FAIL() << "commit must refuse a state no arm claims";
    }
    catch (const fs::NotAllowedUpdateState &e)
    {
        EXPECT_NE(std::string(e.what()).find("not allowed: 3"), std::string::npos)
            << "the refusal does not name the state it refused: " << e.what();
    }
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

/* The verdict is a statement about durable state; the collaborator underneath
 * it opens a RAUC configuration that a device may not have. Constructed first,
 * its error names a missing configuration where the operator's problem is the
 * target slot. Nothing is staged either way; what differs is which diagnosis
 * they get. */
TEST(FSUpdateSeam, ApplicationRollbackRefusesAnUnprovisionedTargetBeforeItsCollaborator)
{
    auto env = env_with("0");
    fs::FSUpdate updater(env, quiet_logger());

    try
    {
        updater.rollback_application();
        ADD_FAILURE() << "a switch to a slot that was never provisioned was not refused";
    }
    catch (const fs::GenericException &e)
    {
        EXPECT_EQ(e.errorno, ENOENT);
        EXPECT_NE(std::string(e.what()).find("slot B"), std::string::npos);
    }
    catch (const std::exception &e)
    {
        ADD_FAILURE() << "refused with a diagnosis of its own: " << e.what();
    }

    EXPECT_TRUE(env->nothing_staged());
}

/* Positive control on the same shape. Without it the refusal above could be
 * the environment being inert rather than the state being rejected. */
TEST(FSUpdateSeam, CommitOnAReadableIdleStateDoesNotRefuse)
{
    auto env = env_with("0");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_NO_THROW((void)updater.commit_update());
}

/* The state whose meaning contradicts the shape its own handler demanded.
 * FW_UPDATE_REBOOT_FAILED means the bootloader fell back to the proven slot, so
 * the slot the device is running is the committed one -- while the acknowledge
 * predicate required that slot's digit to be uncommitted. Nothing writes the
 * value, but a device can carry it in from an environment edit or a firmware old
 * enough to have written it, and then it could not leave: commit refused,
 * rollback does not admit the state, both switch verbs require idle, and an
 * install is blocked by the pending state.
 *
 * The consuming layer counts this state's code among the failed ones and calls
 * commit to acknowledge it, so a device that cannot leave the state fails that
 * call on every boot and the deadline timer reboots it once per period. Being
 * able to leave is the whole fix.
 */
TEST(FSUpdateSeam, CommitRecoversTheInducedFailedRebootState)
{
    auto env = env_with("1");
    env->set("BOOT_A_LEFT", "1");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_TRUE(updater.commit_update());
    env->flushEnvironment();

    EXPECT_EQ(env->at("update_reboot_state"), "0") << "the device cannot leave the state";
    /* The pending state gated the routine mark-good, so the running slot is one
     * boot from dropping out of the rotation until this is put back. */
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    /* Nothing here proves which slot failed to boot, so nothing is condemned. */
    EXPECT_EQ(env->at("update"), "0000");
}

/* On the shape the old handler wanted, the digit of the slot the device is
 * running is the one thing the recovery can prove -- it booted. Settling it is
 * right; marking it bad, which is what the handler did, takes the slot the
 * device is running out of the rotation.
 */
TEST(FSUpdateSeam, CommitSettlesTheRunningSlotInsteadOfCondemningIt)
{
    auto env = env_with("1");
    env->set("update", "1000");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_TRUE(updater.commit_update());
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0000") << "the running slot was condemned instead of settled";
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* "Uncommitted" is a bit, so '3' is as uncommitted as '1'. Left standing, a
 * later install marking the other firmware slot uncommitted would put two
 * uncommitted digits in the field, and from then on every read of it raises --
 * a worse place than the state this recovery exists to leave. The bad bit is
 * kept: the slot booted, which says nothing about a mark it carried in.
 */
TEST(FSUpdateSeam, CommitSettlesTheUncommittedBitWhateverTheDigit)
{
    auto env = env_with("1");
    env->set("update", "3000");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_TRUE(updater.commit_update());
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "2000") << "the uncommitted bit survived the migration";
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* The install gate's green branch is where a stale digit is settled. The state
 * said nothing is pending, so a digit claiming otherwise is residue -- and the
 * install is about to put its own target digit next to it, which is the pair
 * the validator rejects. The install itself fails here, because there is no
 * bundle, and that is the point: the repair sits outside the install's own
 * transaction, so it stands whether the install does or not.
 */
TEST(FSUpdateSeam, AnInstallSettlesAStaleDigitBeforeItStarts)
{
    auto env = env_with("0");
    env->set("update", "1000");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_ANY_THROW(updater.update_firmware("/nonexistent/bundle.raucb"));
    env->flushEnvironment();

    /* Index 0 is the running slot's firmware digit; the fixture names slot A. */
    EXPECT_EQ(env->at("update").at(0), '0') << "the stale digit survived into the install";
    EXPECT_TRUE(validate_update_bits(env->at("update")))
        << "the install left a field the read path rejects: " << env->at("update");
}

/* Every state that pairs a stored value with a slot-bitfield shape, refused
 * because the shape is absent. Naming the state alone leaves the two reasons a
 * refusal can happen indistinguishable -- no verb owns the state, or the verb
 * that owns it found its precondition false -- and only the second is
 * actionable. Each row therefore has to say what the state expected to find.
 *
 * The bitfield is the settled one, so no case can be satisfied by accident: a
 * row that stops refusing means the state was reached by an arm that does not
 * require its own precondition. */
class CommitPreconditionRefusal : public ::testing::TestWithParam<std::pair<const char *, const char *>>
{
};

TEST_P(CommitPreconditionRefusal, NamesTheStateAndWhatItExpected)
{
    const std::string state = GetParam().first;
    const std::string expectation = GetParam().second;
    auto env = env_with(state);
    fs::FSUpdate updater(env, quiet_logger());

    try
    {
        (void)updater.commit_update();
        FAIL() << "commit must refuse state " << state << " when its slot precondition does not hold";
    }
    catch (const fs::NotAllowedUpdateState &e)
    {
        const std::string what(e.what());
        EXPECT_NE(what.find("not allowed: " + state), std::string::npos)
            << "the refusal does not name the state: " << what;
        EXPECT_NE(what.find(expectation), std::string::npos)
            << "the refusal does not say what state " << state << " expected: " << what;
    }
}

INSTANTIATE_TEST_SUITE_P(
    StatesWithASlotPrecondition, CommitPreconditionRefusal,
    /* State 1 is not in this list: it carries no slot precondition any more --
     * it is recoverable from any shape, see the two cases above. */
    ::testing::Values(std::make_pair("2", "uncommitted firmware slot"),
                      std::make_pair("3", "uncommitted application slot"),
                      std::make_pair("4", "uncommitted firmware and application slot"),
                      std::make_pair("5", "uncommitted firmware slot"),
                      std::make_pair("6", "uncommitted application slot")));


/* --- the bad-mark writer: until now driven by nothing at all --- */

/* The routine case. Digits are indexed fw_a, app_a, fw_b, app_b, so marking
 * application slot B bad moves the last one. */
TEST(FSUpdateSeam, SetUpdateStateBadMarksACommittedSlot)
{
    auto env = env_with("0");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_EQ(updater.set_update_state_bad('B', 1), 0);
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0002");
}

/* Marking twice must not stage a second write: the verb reports success and
 * leaves the environment alone, so a caller that marks on every boot does not
 * write the bootloader environment on every boot. */
TEST(FSUpdateSeam, SetUpdateStateBadWritesNothingWhenTheSlotIsAlreadyBad)
{
    auto env = env_with("0");
    env->set("update", "0002");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_EQ(updater.set_update_state_bad('B', 1), 0);
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0002");
    EXPECT_TRUE(env->writes_of("update").empty());
}

/* Marking a slot bad says nothing about whether an update is still in flight
 * on it, so the mark sets the bad bit and leaves the other fact alone. The
 * verb used to assign a bare bad digit, which destroyed it: the pending-update
 * predicate then answered false and an automatic revert took the slot-switch
 * path instead of the pending one, reaching the right end state by the wrong
 * route. */
TEST(FSUpdateSeam, SetUpdateStateBadKeepsTheInFlightBitOfTheSlotItMarks)
{
    auto env = env_with("0");
    env->set("update", "0001");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_EQ(updater.set_update_state_bad('B', 1), 0);
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0003");
}

/* A slot already carrying both facts is already bad: the early exit holds, so
 * repeated marking still stages no write. */
TEST(FSUpdateSeam, SetUpdateStateBadWritesNothingWhenBothFactsAreAlreadySet)
{
    auto env = env_with("0");
    env->set("update", "0003");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_EQ(updater.set_update_state_bad('B', 1), 0);
    env->flushEnvironment();

    EXPECT_TRUE(env->writes_of("update").empty());
    EXPECT_EQ(env->at("update"), "0003");
}

/* The argument guard, so the three cases above cannot be read as "any input
 * writes something". */
TEST(FSUpdateSeam, SetUpdateStateBadRefusesAnUnknownSlotAndAnUnknownDimension)
{
    auto env = env_with("0");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_EQ(updater.set_update_state_bad('C', 1), EINVAL);
    EXPECT_EQ(updater.set_update_state_bad('A', 2), EINVAL);
    env->flushEnvironment();

    EXPECT_TRUE(env->writes_of("update").empty());
}

/* The confirm path's success branch writes the committed digit as a literal, so
 * a slot that carries a bad mark loses it the moment its update is committed.
 * digit_settled() states the opposite rule in the same tree -- a bad mark is a
 * verdict about the slot, and finishing an update is not evidence against it --
 * and the settle path already follows it.
 *
 * The success predicate reads boot_order alone and never looks at the digit, so
 * this branch is reachable with any digit standing: a slot marked bad after it
 * booted still reaches the commit.
 */
TEST(FSUpdateSeam, CommitOfAFirmwareUpdateKeepsTheBadMarkOnTheRunningSlot)
{
    auto env = env_with("2"); /* INCOMPLETE_FW_UPDATE */
    env->set("rauc_cmd", "rauc.slot=B");
    env->set("update", "0030"); /* fw_b: uncommitted and bad */
    env->set("BOOT_ORDER_OLD", "A B");
    env->set("BOOT_ORDER", "B A");
    fs::FSUpdate updater(env, quiet_logger());

    EXPECT_TRUE(updater.commit_update());
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0020") << "the commit cleared the bad bit along with the uncommitted one";
}

}
