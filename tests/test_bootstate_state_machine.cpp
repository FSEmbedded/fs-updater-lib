#include <gtest/gtest.h>

#include "handle_update/handleUpdate.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "handle_update/reboot_state.h"
#include "uboot_interface/IUBootEnv.h"
#include "uboot_interface/uboot_exceptions.h"
#include "support/fake_uboot_env.h"
#include "util/posix_utils.h"

extern "C" {
#include <unistd.h>
}

#include <climits>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

using test_support::FakeUBootEnv;

struct BootstateFixture : public ::testing::Test
{
    /* Board-shape presets. Digits: index 0 fw_a, 1 app_a, 2 fw_b, 3 app_b. */
    std::shared_ptr<FakeUBootEnv> make_env(std::map<std::string, std::string> overrides)
    {
        std::map<std::string, std::string> env = {
            {"update", "0000"},
            {"update_reboot_state", "0"},
            {"BOOT_ORDER", "A B"},
            {"BOOT_ORDER_OLD", "A B"},
            {"BOOT_A_LEFT", "3"},
            {"BOOT_B_LEFT", "3"},
            {"rauc_cmd", "rauc.slot=A"},
            {"application", "A"},
        };
        for (auto &kv : overrides)
        {
            env[kv.first] = kv.second;
        }
        return std::make_shared<FakeUBootEnv>(env);
    }

    std::unique_ptr<updater::Bootstate> make_bootstate(const std::shared_ptr<FakeUBootEnv> &env)
    {
        auto sink = std::make_shared<logger::LoggerSinkEmpty>(logger::logLevel::ERROR);
        auto handler = logger::LoggerHandler::initLogger(sink);
        return std::unique_ptr<updater::Bootstate>(new updater::Bootstate(env, handler));
    }
};

/* --- switch finalize: reboot landed on the switched (preferred) slot --- */

TEST_F(BootstateFixture, SwitchCommitAfterPreferredBootAdoptsSwitchedOrder)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_B_LEFT", "2"},
                         {"rauc_cmd", "rauc.slot=B"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("BOOT_ORDER"), "B A");
    EXPECT_EQ(env->at("BOOT_ORDER_OLD"), "B A");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
    EXPECT_EQ(env->at("update"), "0000");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* --- switch finalize: switch boot never landed, U-Boot reverted --- */

TEST_F(BootstateFixture, SwitchCommitAfterRevertRestoresOrderAndMarksDeadSlotBad)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_A_LEFT", "2"},
                         {"BOOT_B_LEFT", "0"},
                         {"rauc_cmd", "rauc.slot=A"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    /* The proven slot stays preferred and the slot that failed its switch boot
     * is recorded as bad in the bitfield. Both boot budgets are restored, the
     * dead slot's included: boot selection never reads the bitfield, the gate
     * has eroded the proven slot's budget and it has to come back in this same
     * flush, and the digit is what keeps the switch verbs off the dead slot
     * until an install overwrites its payload. */
    EXPECT_EQ(env->at("BOOT_ORDER"), "A B");
    EXPECT_EQ(env->at("update"), "0020");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* --- true rollback of a pending fw update (reverted via drained counter) --- */

TEST_F(BootstateFixture, FwRollbackCommitSettlesSlotAndRestoresOldOrder)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"update", "0010"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_B_LEFT", "0"},
                         {"rauc_cmd", "rauc.slot=A"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("BOOT_ORDER"), "A B");
    EXPECT_EQ(env->at("update"), "0000");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* --- rollback of an installed update whose reboot never happened --- */

TEST_F(BootstateFixture, PreRebootRollbackSettlesTheAbandonedSlot)
{
    /* The install wrote the other slot and the reboot into it never happened,
     * so that slot is the one being abandoned. Settling the running slot
     * instead left the abandoned one recorded uncommitted while the machine
     * reported idle -- invisible to every status query, and enough to have a
     * later slot switch refused with nothing to explain it. */
    auto env = make_env({{"update_reboot_state", "2"},
                         {"update", "0010"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"rauc_cmd", "rauc.slot=A"}});
    auto bootstate = make_bootstate(env);

    bootstate->firmware_rollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0000");
    EXPECT_EQ(env->at("BOOT_ORDER"), "A B");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* --- an install interrupted while its target slot was deactivated --- */

TEST_F(BootstateFixture, InterruptedInstallWithSingleSlotOrderRecovers)
{
    /* Writing a slot starts by taking it out of the boot order, so a machine
     * stopped inside that window comes back with a single-slot order. That is a
     * value the bootloader backend writes deliberately -- marking a slot bad
     * produces it too -- and it has to stay readable: rejecting it left a device
     * on which no verb could report a state, let alone recover one.
     * The recovery is the ordinary failed-reboot path: the half-written slot is
     * recorded bad and the previous order restored. */
    auto env = make_env({{"update_reboot_state", "2"},
                         {"update", "1000"},
                         {"BOOT_ORDER", "B"},
                         {"BOOT_ORDER_OLD", "B A"},
                         {"BOOT_A_LEFT", "0"},
                         {"rauc_cmd", "rauc.slot=B"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmPendingFirmwareUpdate();
    env->flushEnvironment();

    EXPECT_EQ(env->at("BOOT_ORDER"), "B A");
    EXPECT_EQ(env->at("update"), "2000");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* --- the same rollback, committed BEFORE its reboot --- */

TEST_F(BootstateFixture, FwRollbackCommitBeforeRebootIsRefused)
{
    /* Same durable state as the test above, but the device is still running the
     * slot the rollback reverts away from -- so that slot is the head of the
     * boot order and its own digit is still uncommitted. Read through the next
     * slot's digit alone this is indistinguishable from a slot switch that
     * landed, and adopting the order here would accept the update the caller
     * asked to revert. */
    auto env = make_env({{"update_reboot_state", "7"},
                         {"update", "0010"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_B_LEFT", "0"},
                         {"rauc_cmd", "rauc.slot=B"}});
    auto bootstate = make_bootstate(env);

    EXPECT_THROW(bootstate->confirmUpdateRollback(), updater::MissingReboot);

    /* Nothing may be staged on the refusal: a caller that retries after the
     * reboot must find the state it prepared. */
    EXPECT_EQ(env->at("BOOT_ORDER"), "B A");
    EXPECT_EQ(env->at("BOOT_ORDER_OLD"), "A B");
    EXPECT_EQ(env->at("update"), "0010");
    EXPECT_EQ(env->at("update_reboot_state"), "7");
}

/* --- app rollback commit settles the rolled-back app slot --- */

TEST_F(BootstateFixture, AppRollbackCommitSettlesAppSlotAndRestoresBudget)
{
    /* The rollback cycle's pending-state boots drained the running slot's
     * budget (mark-good is gated then); commit must re-arm it, else the next
     * reboot silently selects the other slot. */
    auto env = make_env({{"update_reboot_state", "11"},
                         {"update", "0001"},
                         {"application", "A"},
                         {"BOOT_A_LEFT", "0"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0000");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
    EXPECT_EQ(env->at("BOOT_ORDER"), "A B");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
}

/* --- combined rollback commit settles both slots --- */

TEST_F(BootstateFixture, CombinedRollbackCommitSettlesBothSlots)
{
    auto env = make_env({{"update_reboot_state", "12"},
                         {"update", "0011"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_B_LEFT", "0"},
                         {"rauc_cmd", "rauc.slot=A"},
                         {"application", "A"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("BOOT_ORDER"), "A B");
    EXPECT_EQ(env->at("update"), "0000");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
}

/* --- routing: pendingUpdateRollback distinguishes revert from landed boot --- */

TEST_F(BootstateFixture, PendingRollbackTrueOnRevertShape)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_B_LEFT", "0"},
                         {"rauc_cmd", "rauc.slot=A"}});
    auto bootstate = make_bootstate(env);

    auto state = update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING;
    EXPECT_TRUE(bootstate->pendingUpdateRollback(state));
}

TEST_F(BootstateFixture, PendingRollbackFalseOncePreferredSlotBooted)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_B_LEFT", "2"},
                         {"rauc_cmd", "rauc.slot=B"}});
    auto bootstate = make_bootstate(env);

    auto state = update_definitions::UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING;
    EXPECT_FALSE(bootstate->pendingUpdateRollback(state));
}

/* --- firmware_reboot: the switch-finalize eligibility check --- */

TEST_F(BootstateFixture, FirmwareRebootTrueOncePreferredSlotBooted)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_B_LEFT", "2"},
                         {"rauc_cmd", "rauc.slot=B"}});
    auto bootstate = make_bootstate(env);

    EXPECT_TRUE(bootstate->firmware_reboot());
}

TEST_F(BootstateFixture, FirmwareRebootFalseBeforeTheSwitchReboot)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"rauc_cmd", "rauc.slot=A"}});
    auto bootstate = make_bootstate(env);

    EXPECT_FALSE(bootstate->firmware_reboot());
}

/* --- guard: commit in a non-rollback state is refused --- */

TEST_F(BootstateFixture, RollbackCommitRefusedInIdleState)
{
    auto env = make_env({});
    auto bootstate = make_bootstate(env);

    EXPECT_ANY_THROW(bootstate->confirmUpdateRollback());
}

/* --- application_reboot: must scan every loop device, not only loop0 --- */

/* Builds a temp "/sys/class/block"-shaped tree: <root>/<name>/loop/backing_file
 * for each entry. Removed on destruction. */
class FakeSysfsBlockRoot
{
  public:
    explicit FakeSysfsBlockRoot(const std::map<std::string, std::string> &loop_backing_files)
        : entries_(loop_backing_files)
    {
        std::string tmpl = "/tmp/sysfs-block-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        EXPECT_NE(::mkdtemp(buf.data()), nullptr);
        root_ = buf.data();

        for (const auto &kv : loop_backing_files)
        {
            const std::string loop_dir = fs::util::path_join(root_, kv.first + "/loop");
            EXPECT_TRUE(fs::util::mkdir_p(loop_dir));
            std::ofstream backing_file(fs::util::path_join(loop_dir, "backing_file"));
            backing_file << kv.second << "\n";
        }
    }

    ~FakeSysfsBlockRoot()
    {
        for (const auto &kv : entries_)
        {
            (void)fs::util::remove_file(fs::util::path_join(root_, kv.first + "/loop/backing_file"));
        }
        /* Best-effort: rmdir the tree bottom-up. Leaked on failure — the
         * sandbox tmp dir gets reclaimed anyway; tests don't assert on it. */
    }

    [[nodiscard]] const std::string &root() const { return root_; }

  private:
    std::string root_;
    std::map<std::string, std::string> entries_;
};

TEST_F(BootstateFixture, ApplicationRebootScansAllLoopDevicesNotOnlyLoop0)
{
    /* loop0 backs something unrelated; the app image landed on loop1 because
     * another consumer grabbed loop0 first — a real scenario in the
     * container app-update stack. application_reboot() must still find it. */
    FakeSysfsBlockRoot sysfs({{"loop0", "/some/other/image.squashfs"},
                              {"loop1", "/data/app/app_a.squashfs"}});
    auto env = make_env({{"application", "A"}});
    auto bootstate = make_bootstate(env);

    EXPECT_EQ(bootstate->application_reboot(sysfs.root()),
              updater::Bootstate::AppImageState::ACTIVE_SLOT_MOUNTED);
}

TEST_F(BootstateFixture, ApplicationRebootOtherSlotWhenNoLoopDeviceMatchesExpectedSlot)
{
    FakeSysfsBlockRoot sysfs({{"loop0", "/some/other/image.squashfs"}});
    auto env = make_env({{"application", "A"}});
    auto bootstate = make_bootstate(env);

    EXPECT_EQ(bootstate->application_reboot(sysfs.root()),
              updater::Bootstate::AppImageState::OTHER_SLOT_MOUNTED);
}

/* Pre-mount there are zero loop devices regardless of reboot state - the
 * scan must answer NOT_MOUNTED, not throw. */
TEST_F(BootstateFixture, ApplicationRebootNotMountedOnEmptySysfsRoot)
{
    FakeSysfsBlockRoot sysfs({});
    auto env = make_env({{"application", "A"}});
    auto bootstate = make_bootstate(env);

    EXPECT_EQ(bootstate->application_reboot(sysfs.root()),
              updater::Bootstate::AppImageState::NOT_MOUNTED);
}

TEST_F(BootstateFixture, ApplicationRebootNotMountedWhenBackingFileUnreadable)
{
    /* A loop* entry exists but exposes no readable backing_file (e.g. the
     * device is not bound) - same answer as no loop devices at all. */
    FakeSysfsBlockRoot sysfs({});
    ASSERT_TRUE(fs::util::mkdir_p(fs::util::path_join(sysfs.root(), "loop0/loop")));
    auto env = make_env({{"application", "A"}});
    auto bootstate = make_bootstate(env);

    EXPECT_EQ(bootstate->application_reboot(sysfs.root()),
              updater::Bootstate::AppImageState::NOT_MOUNTED);
}

/* An unreadable sysfs root stays a genuine error - distinct from the
 * expected zero-loop-devices pre-mount state. */
TEST_F(BootstateFixture, ApplicationRebootThrowsWhenSysfsRootUnreadable)
{
    auto env = make_env({{"application", "A"}});
    auto bootstate = make_bootstate(env);

    EXPECT_THROW((void)bootstate->application_reboot("/nonexistent/sysfs-block-root"),
                 updater::GetLoopDevices);
}

/* --- app-only commit: confirmPendingApplicationUpdate --- */

TEST_F(BootstateFixture, ConfirmPendingApplicationUpdateAfterMatchingRebootCommits)
{
    /* Rebooted into the newly-installed B: the loop-mounted image now
     * matches the (already-flipped) 'application' var. */
    FakeSysfsBlockRoot sysfs({{"loop0", "/data/app/images/app_b.squashfs"}});
    auto env = make_env({{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmPendingApplicationUpdate(sysfs.root());
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0000");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

TEST_F(BootstateFixture, ConfirmPendingApplicationUpdateWithoutRebootThrowsMissingReboot)
{
    /* application was flipped to B at install time, but nobody rebooted
     * yet - the loop-mounted image is still the old A. */
    FakeSysfsBlockRoot sysfs({{"loop0", "/data/app/images/app_a.squashfs"}});
    auto env = make_env({{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    EXPECT_THROW(bootstate->confirmPendingApplicationUpdate(sysfs.root()), updater::MissingReboot);

    /* Nothing was staged before the throw. */
    EXPECT_EQ(env->at("update"), "0001");
    EXPECT_EQ(env->at("update_reboot_state"), "3");
}

TEST_F(BootstateFixture, ConfirmPendingApplicationUpdateWithNothingMountedRefusesCommit)
{
    /* No app image loop-mounted at all: the update never took effect, so
     * neither "missing reboot" nor a silent commit success would be true. */
    FakeSysfsBlockRoot sysfs({});
    auto env = make_env({{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    EXPECT_THROW(bootstate->confirmPendingApplicationUpdate(sysfs.root()), updater::GetLoopDevices);

    /* Nothing was staged before the throw. */
    EXPECT_EQ(env->at("update"), "0001");
    EXPECT_EQ(env->at("update_reboot_state"), "3");
}

TEST_F(BootstateFixture, PendingUpdateRollbackSettlesWhenNothingMounted)
{
    /* The counterpart of the case above, and deliberately the other answer.
     * There an *update* had never taken effect, so committing it would have
     * confirmed something that never ran. Here the rollback was decided and
     * enacted before this state was written: the slot switch has happened and
     * no boot changes it back, so an unmountable image leaves nothing to
     * validate -- only bookkeeping to finish.
     *
     * Refusing here left the device with no verb at all: commit raised,
     * rollback answered that a commit is required, both switch verbs want
     * idle, and an install is blocked by the pending state. */
    FakeSysfsBlockRoot sysfs({});
    auto env = make_env({{"update_reboot_state", "8"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    auto state = update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING;
    bool settles = false;
    ASSERT_NO_THROW(settles = bootstate->pendingUpdateRollback(state, sysfs.root()));
    EXPECT_TRUE(settles) << "the commit's precondition must hold, or this state has no exit";

    /* The probe answers; it does not write. */
    EXPECT_EQ(env->at("update_reboot_state"), "8");
}

/* --- app-only rollback: applicaton_rollback --- */

TEST_F(BootstateFixture, ApplicatonRollbackAfterMatchingRebootSetsRollbackPending)
{
    /* Already rebooted into the bad new slot B (mounted image matches
     * 'application') - can't un-boot live, so flip back and require one
     * more reboot before this settles (picked up by confirmUpdateRollback,
     * see AppRollbackCommitSettlesAppSlotAndRestoresBudget above). */
    FakeSysfsBlockRoot sysfs({{"loop0", "/data/app/images/app_b.squashfs"}});
    auto env = make_env({{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);
    bool rollback_called = false;

    bootstate->applicaton_rollback(
        [&]() {
            rollback_called = true;
            env->addVariable("application", "A");
        },
        sysfs.root());
    env->flushEnvironment();

    EXPECT_TRUE(rollback_called);
    EXPECT_EQ(env->at("application"), "A");
    EXPECT_EQ(env->at("update_reboot_state"), "8");
    EXPECT_EQ(env->at("update"), "0001");
}

TEST_F(BootstateFixture, ApplicatonRollbackBeforeRebootClearsAbandonedSlotAndFlipsBack)
{
    /* Never rebooted into the newly-installed B: the mounted image is still
     * the old A, so application_reboot() sees a mismatch and takes the
     * immediate-settle path - no second reboot needed.
     *
     * Order-sensitive: get_update_bit() re-reads 'application' AFTER the
     * rollback callback stages the A flip. It must see the pre-flush
     * (still-B) value to clear the B slot's bit, not A's - only correct
     * with FakeUBootEnv's deferred-write semantics (see class comment). */
    FakeSysfsBlockRoot sysfs({{"loop0", "/data/app/images/app_a.squashfs"}});
    auto env = make_env({{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    bootstate->applicaton_rollback([&]() { env->addVariable("application", "A"); }, sysfs.root());
    env->flushEnvironment();

    EXPECT_EQ(env->at("application"), "A");
    EXPECT_EQ(env->at("update"), "0000") << "must clear the abandoned B slot's bit (index 3), not A's (index 1)";
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

TEST_F(BootstateFixture, ApplicatonRollbackWithNothingMountedSettlesImmediately)
{
    /* Pre-mount revert (e.g. bootguard trial exhaustion): no app image is
     * loop-mounted at all. Must settle exactly like the not-yet-rebooted
     * case - a throw here would loop the revert forever. */
    FakeSysfsBlockRoot sysfs({});
    auto env = make_env({{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    bootstate->applicaton_rollback([&]() { env->addVariable("application", "A"); }, sysfs.root());
    env->flushEnvironment();

    EXPECT_EQ(env->at("application"), "A");
    EXPECT_EQ(env->at("update"), "0000");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

TEST_F(BootstateFixture, PendingUpdateRollbackTrueForAppRollbackPendingBlocksSecondRollback)
{
    /* Mirrors the guard FSUpdate::rollback_application() (fsupdate.cpp) uses
     * to refuse a second rollback while the first still awaits its
     * confirming reboot: it throws "Commit for rollback required" when this
     * returns true. FSUpdate itself has no test seam (concrete UBoot ctor
     * dependency), so this pins the Bootstate primitive the guard reads,
     * not the throw statement itself. */
    auto env = make_env({{"update_reboot_state", "8"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    auto state = update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING;
    EXPECT_TRUE(bootstate->pendingUpdateRollback(state));
}

/* --- characterization: a known, unfixed gap --- */

TEST_F(BootstateFixture, PendingApplicationUpdateFalseAfterCrashBeforeApplicationFlip)
{
    /* Characterizes a known, pre-existing wedge (shared with the legacy app
     * path, not introduced by the RAUC path). A crash between
     * fsupdate.cpp's pre-install flush (INCOMPLETE_APP_UPDATE + next-slot
     * bit, flushed) and applicationUpdate::install()'s later 'application'
     * flip (its own separate, later flush) leaves 'application' pointing at
     * the OLD slot while the update bitfield marks the NEW (never-booted)
     * slot uncommitted. pendingApplicationUpdate() reads the CURRENT slot's
     * bit (still '0', committed) - not the pending one - so it reports
     * "nothing pending" even though update_reboot_state says otherwise.
     *
     * Consequence (traced, not exercised here - FSUpdate has no test seam):
     * FSUpdate::commit_update() falls through every predicate and throws
     * NotAllowedUpdateState; rollback_application() takes the committed-app
     * switch-back branch and throws ECANCELED on the other slot's
     * STATE_UPDATE_UNCOMMITED check. The device is wedged - manual recovery
     * required - until a fresh install overwrites the stale bit. */
    auto env = make_env({{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "A"}});
    auto bootstate = make_bootstate(env);

    EXPECT_FALSE(bootstate->pendingApplicationUpdate());
}

TEST_F(BootstateFixture, FirmwareDetectorsDoNotFireForAppOnlyUpdate)
{
    /* An app-only pending-commit state never touches BOOT_ORDER; none of the
     * firmware-side detectors react to it - nothing will auto-revert a bad
     * app the way a failed firmware boot auto-reverts via the U-Boot boot
     * counter. This is exactly why an explicit app health gate is
     * necessary, rather than relying on the firmware auto-revert path. */
    auto env = make_env({{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    EXPECT_FALSE(bootstate->pendingFirmwareUpdate());
    EXPECT_FALSE(bootstate->failedFirmwareUpdate());
    EXPECT_FALSE(bootstate->failedRebootFirmwareUpdate());
    EXPECT_EQ(env->at("BOOT_ORDER"), env->at("BOOT_ORDER_OLD"));
}

/* --- install interrupted before its target was ever activated --- */

TEST_F(BootstateFixture, InstallInterruptedBeforeActivationSettles)
{
    /* Power lost between the install's env write and the bootloader backend
     * taking the target out of the rotation. All three reboot predicates need
     * the two boot orders to differ, so this state once had no verb that left
     * it: commit threw, rollback wrote nothing, install was refused. */
    auto env = make_env({{"update_reboot_state", "2"}, {"update", "0010"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmPendingFirmwareUpdate();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0020");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
    /* Nothing was ever staged, so there is no order to restore. */
    EXPECT_EQ(env->at("BOOT_ORDER"), "A B");
    EXPECT_EQ(env->at("BOOT_ORDER_OLD"), "A B");
}

TEST_F(BootstateFixture, InterruptedInstallQuarantinesTheSlotCarryingTheDigit)
{
    /* The same state after it decayed: the mark-good gate withheld the counter
     * reset, the proven slot's budget reached zero, and the selector fell
     * through to the slot the interrupted install was targeting. The quarantine
     * has to follow the digit - get_update_bit() resolves against the running
     * slot and would mark the healthy one here. */
    auto env = make_env({{"update_reboot_state", "2"},
                         {"update", "0010"},
                         {"BOOT_A_LEFT", "0"},
                         {"BOOT_B_LEFT", "2"},
                         {"rauc_cmd", "rauc.slot=B"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmPendingFirmwareUpdate();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0020");
    EXPECT_NE(env->at("update"), "2010");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
    /* Re-arming is what keeps the next boot from dropping the proven slot out
     * of the rotation and landing on the quarantined one. */
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
}

TEST_F(BootstateFixture, SettledInterruptedInstallIsNotPendingAgain)
{
    auto env = make_env({{"update_reboot_state", "2"}, {"update", "0010"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmPendingFirmwareUpdate();
    env->flushEnvironment();

    EXPECT_FALSE(bootstate->pendingFirmwareUpdate());
    EXPECT_THROW(bootstate->confirmPendingFirmwareUpdate(), updater::ConfirmPendingFirmwareUpdate);
}

/* --- windows that already have an owner must keep it --- */

TEST_F(BootstateFixture, DeactivatedTargetStillTakesTheFailedRebootBranch)
{
    /* Interrupted later, once the backend had taken the target out of the
     * rotation and zeroed its budget: the orders differ and a budget is zero,
     * so the failed-reboot branch owns this one and restores the full order. */
    auto env = make_env({{"update_reboot_state", "2"},
                         {"update", "1000"},
                         {"BOOT_ORDER", "B"},
                         {"BOOT_ORDER_OLD", "B A"},
                         {"BOOT_A_LEFT", "0"},
                         {"BOOT_B_LEFT", "2"},
                         {"rauc_cmd", "rauc.slot=B"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmPendingFirmwareUpdate();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "2000");
    EXPECT_EQ(env->at("BOOT_ORDER"), "B A");
    EXPECT_EQ(env->at("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env->at("BOOT_B_LEFT"), "3");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

TEST_F(BootstateFixture, LandedRebootStillTakesTheSuccessBranch)
{
    auto env = make_env({{"update_reboot_state", "2"},
                         {"update", "0010"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"BOOT_B_LEFT", "2"},
                         {"rauc_cmd", "rauc.slot=B"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmPendingFirmwareUpdate();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0000");
    EXPECT_EQ(env->at("BOOT_ORDER_OLD"), "B A");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* --- total decode of update_reboot_state (smoke; the full matrix is in
 * test_reboot_state_totality.cpp) --- */

TEST_F(BootstateFixture, ReadRebootStateIsTotalOverGarbageAndAbsence)
{
    using update_definitions::UBootBootstateFlags;

    auto env = make_env({});

    env->set("update_reboot_state", "0x02");
    EXPECT_EQ(update_definitions::read_update_reboot_state(*env), UBootBootstateFlags::UNKNOWN_STATE);

    env->set("update_reboot_state", "12abc");
    EXPECT_EQ(update_definitions::read_update_reboot_state(*env), UBootBootstateFlags::UNKNOWN_STATE);

    env->set("update_reboot_state", "02");
    EXPECT_EQ(update_definitions::read_update_reboot_state(*env), UBootBootstateFlags::UNKNOWN_STATE);

    env->unset("update_reboot_state");
    EXPECT_EQ(update_definitions::read_update_reboot_state(*env), UBootBootstateFlags::UNKNOWN_STATE);

    env->set("update_reboot_state", "12");
    EXPECT_EQ(update_definitions::read_update_reboot_state(*env), UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK);
}

/* --- the recovery state is never idle and never actionable --- */

/* Uninterpretable durable content must not read as "nothing going on": a
 * device whose state cannot be told is exactly the device an operator has to
 * be told about. */
TEST_F(BootstateFixture, UninterpretableStateIsNeverIdle)
{
    for (const std::string &seed : {std::string("13"), std::string("0x02"), std::string(""), std::string("012"),
                                    std::string("abc")})
    {
        auto env = make_env({});
        env->set("update_reboot_state", seed);
        auto bootstate = make_bootstate(env);

        EXPECT_EQ(update_definitions::read_update_reboot_state(*env),
                  update_definitions::UBootBootstateFlags::UNKNOWN_STATE)
            << "seed: " << seed;
        EXPECT_FALSE(bootstate->noUpdateProcessing()) << "seed: " << seed;
    }

    auto env = make_env({});
    env->unset("update_reboot_state");
    auto bootstate = make_bootstate(env);
    EXPECT_FALSE(bootstate->noUpdateProcessing());
}

/* The commit door routes on these predicates; every "no" is what makes it
 * fall through to its refusal instead of acting on a guessed state. The bit
 * shapes are chosen so each predicate actually reaches the reboot-state read
 * rather than returning early on the bitfield: running slot A with app A,
 * 1000 and 0100 open the current-slot branches, 1100 the combined one, 0010
 * and 0011 the next-slot branches, 0001 the app-only next-slot branch that
 * is the sole way into the failed-application check. */
TEST_F(BootstateFixture, UninterpretableStateAnswersNoToEveryCommitPredicate)
{
    for (const std::string &bits : {std::string("1000"), std::string("0100"), std::string("1100"),
                                    std::string("0010"), std::string("0011"), std::string("0001")})
    {
        for (const std::string &seed : {std::string("13"), std::string("0x02"), std::string("")})
        {
            auto env = make_env({{"update", bits}});
            env->set("update_reboot_state", seed);
            auto bootstate = make_bootstate(env);
            const std::string where = bits + " / \"" + seed + "\"";

            EXPECT_FALSE(bootstate->pendingApplicationUpdate()) << where;
            EXPECT_FALSE(bootstate->pendingFirmwareUpdate()) << where;
            EXPECT_FALSE(bootstate->pendingApplicationFirmwareUpdate()) << where;
            EXPECT_FALSE(bootstate->failedFirmwareUpdate()) << where;
            EXPECT_FALSE(bootstate->failedRebootFirmwareUpdate()) << where;
            EXPECT_FALSE(bootstate->failedApplicationUpdate()) << where;
            EXPECT_FALSE(bootstate->noUpdateProcessing()) << where;

            auto state = update_definitions::read_update_reboot_state(*env);
            EXPECT_FALSE(bootstate->pendingUpdateRollback(state)) << where;

            /* Asking must not write. */
            EXPECT_TRUE(env->writes_of("update_reboot_state").empty()) << where;
        }
    }
}

/* The rollback-commit path refuses by a named exception and stages nothing. */
TEST_F(BootstateFixture, RollbackCommitRefusesUninterpretableStateByName)
{
    auto env = make_env({{"update", "1100"}});
    env->set("update_reboot_state", "13");
    auto bootstate = make_bootstate(env);

    EXPECT_THROW(bootstate->confirmUpdateRollback(), updater::ConfirmPendingRollback);
    EXPECT_TRUE(env->writes_of("update_reboot_state").empty());
    EXPECT_TRUE(env->writes_of("update").empty());
    EXPECT_TRUE(env->writes_of("BOOT_ORDER").empty());
    EXPECT_EQ(env->at("update_reboot_state"), "13");
}

/* --- No-Persist: no verb may put an out-of-alphabet value in the env --- */

bool inside_alphabet(const std::string &raw)
{
    return update_definitions::decode_update_reboot_state(raw) !=
           update_definitions::UBootBootstateFlags::UNKNOWN_STATE;
}

using Verb = std::function<void(FakeUBootEnv &, updater::Bootstate &)>;

/* One row per verb, carrying the environment shape under which that verb
 * actually acts. A single shared shape would let most predicates answer from
 * the bitfield before ever reading the reboot state, and the property below
 * would then hold for reasons that have nothing to do with the state. */
struct VerbCase
{
    const char *name;
    Verb run;
    bool stages_reboot_state;
    std::map<std::string, std::string> acting_env;
};

/* Content no reader can interpret. Present-but-empty and absent are separate
 * rows: they reach the accessor as different failures, so one cannot stand in
 * for the other. */
struct RebootStateSeed
{
    const char *label;
    const char *raw;
    bool present;
};

const std::vector<RebootStateSeed> &uninterpretable_seeds()
{
    static const std::vector<RebootStateSeed> seeds = {
        {"13", "13", true},
        {"blank", " ", true},
        {"hex", "0x02", true},
        {"99", "99", true},
        {"012", "012", true},
        {"abc", "abc", true},
        {"fullwidth_zero", "\xef\xbc\x90", true},
        {"empty", "", true},
        {"absent", "", false},
    };
    return seeds;
}

void seed_reboot_state(FakeUBootEnv &env, const RebootStateSeed &seed)
{
    if (seed.present)
    {
        env.set("update_reboot_state", seed.raw);
    }
    else
    {
        env.unset("update_reboot_state");
    }
}

/* The rollback verbs, on shapes where they demonstrably do stage a reboot
 * state when the marker is readable -- see PreRebootRollbackSettlesTheAbandoned
 * Slot and ApplicatonRollbackAfterMatchingRebootSetsRollbackPending. A refusal
 * asserted on a shape the verb would not write anyway proves nothing. */
std::vector<VerbCase> rollback_verbs(FakeSysfsBlockRoot &app_slot_mounted)
{
    return {
        {"firmware_rollback",
         [](FakeUBootEnv &, updater::Bootstate &b) { b.firmware_rollback(); },
         true,
         {{"update", "0010"},
          {"BOOT_ORDER", "B A"},
          {"BOOT_ORDER_OLD", "A B"},
          {"rauc_cmd", "rauc.slot=A"}}},
        {"applicaton_rollback",
         [&app_slot_mounted](FakeUBootEnv &e, updater::Bootstate &b) {
             b.applicaton_rollback([&e]() { e.addVariable("application", "A"); }, app_slot_mounted.root());
         },
         true,
         {{"update", "0001"}, {"application", "B"}}},
    };
}

/* The rollback verbs were the last writers of the reboot state that could
 * still reach an uninterpretable marker. Overwriting it with a canonical value
 * destroys the only evidence of a state this build cannot decode, and
 * handleUpdate.h is installed, so an out-of-tree caller reaches them with no
 * outer guard: they refuse by name, and refuse before staging anything. */
TEST_F(BootstateFixture, RollbackVerbsRefuseUninterpretableStateWithoutStagingAnything)
{
    FakeSysfsBlockRoot app_slot_mounted({{"loop0", "/data/app/images/app_b.squashfs"}});

    for (const VerbCase &verb : rollback_verbs(app_slot_mounted))
    {
        /* Readable marker first: without this the refusal below could be the
         * verb doing nothing on this shape for some other reason. */
        {
            SCOPED_TRACE(std::string(verb.name) + " / readable marker");
            auto env = make_env(verb.acting_env);
            seed_reboot_state(
                *env,
                RebootStateSeed{"readable", (std::string(verb.name) == "firmware_rollback") ? "2" : "3", true});
            auto bootstate = make_bootstate(env);

            ASSERT_NO_THROW(verb.run(*env, *bootstate));
            EXPECT_FALSE(env->writes_of("update_reboot_state").empty());
        }

        for (const RebootStateSeed &seed : uninterpretable_seeds())
        {
            SCOPED_TRACE(std::string(verb.name) + " / " + seed.label);
            auto env = make_env(verb.acting_env);
            seed_reboot_state(*env, seed);
            auto bootstate = make_bootstate(env);

            EXPECT_THROW(verb.run(*env, *bootstate), updater::RebootStateNotInterpretable);

            /* Nothing was attempted at all: not the reboot state, not the boot
             * order, not the boot counters -- and the application rollback
             * callback never ran either. Only the journal can say so; a
             * surviving value cannot tell a refusal from a rewrite of what was
             * already there. */
            EXPECT_TRUE(env->nothing_staged());
            EXPECT_TRUE(env->writes_of("update_reboot_state").empty());
            EXPECT_TRUE(env->writes_of("BOOT_ORDER").empty());
            EXPECT_TRUE(env->writes_of("BOOT_ORDER_OLD").empty());
            EXPECT_TRUE(env->writes_of("BOOT_A_LEFT").empty());
            EXPECT_TRUE(env->writes_of("BOOT_B_LEFT").empty());
            EXPECT_TRUE(env->writes_of("update").empty());
            EXPECT_TRUE(env->writes_of("application").empty());

            env->flushEnvironment();
            if (seed.present)
            {
                EXPECT_EQ(env->at("update_reboot_state"), std::string(seed.raw));
            }
            else
            {
                EXPECT_FALSE(env->holds("update_reboot_state"));
            }
        }
    }
}

/* Every verb of the writing surface, each on the shape it acts on. */
std::vector<VerbCase> every_verb(FakeSysfsBlockRoot &app_slot_mounted, FakeSysfsBlockRoot &nothing_mounted)
{
    std::vector<VerbCase> verbs = {
        {"confirmUpdateRollback",
         [](FakeUBootEnv &, updater::Bootstate &b) { b.confirmUpdateRollback(); },
         true,
         {{"update_reboot_state", "7"},
          {"BOOT_ORDER", "B A"},
          {"BOOT_ORDER_OLD", "A B"},
          {"BOOT_B_LEFT", "2"},
          {"rauc_cmd", "rauc.slot=B"}}},
        {"confirmFailedFirmwareUpdate",
         [](FakeUBootEnv &, updater::Bootstate &b) { b.confirmFailedFirmwareUpdate(); },
         true,
         {{"update_reboot_state", "5"}, {"update", "0010"}}},
        {"confirmFailedRebootFirmwareUpdate",
         [](FakeUBootEnv &, updater::Bootstate &b) { b.confirmFailedRebootFirmwareUpdate(); },
         true,
         {{"update_reboot_state", "1"}, {"update", "1000"}}},
        {"confirmFailedApplicationeUpdate",
         [](FakeUBootEnv &, updater::Bootstate &b) { b.confirmFailedApplicationeUpdate(); },
         true,
         {{"update_reboot_state", "6"}, {"update", "0001"}}},
        {"confirmPendingFirmwareUpdate",
         [](FakeUBootEnv &, updater::Bootstate &b) { b.confirmPendingFirmwareUpdate(); },
         true,
         {{"update_reboot_state", "2"}, {"update", "0010"}}},
        {"confirmPendingApplicationUpdate",
         [&app_slot_mounted](FakeUBootEnv &, updater::Bootstate &b) {
             b.confirmPendingApplicationUpdate(app_slot_mounted.root());
         },
         true,
         {{"update_reboot_state", "3"}, {"update", "0001"}, {"application", "B"}}},
        {"confirmPendingApplicationFirmwareUpdate",
         [](FakeUBootEnv &, updater::Bootstate &b) { b.confirmPendingApplicationFirmwareUpdate(); },
         true,
         {{"update_reboot_state", "4"},
          {"update", "0110"},
          {"BOOT_ORDER", "B A"},
          {"BOOT_ORDER_OLD", "A B"},
          {"rauc_cmd", "rauc.slot=B"},
          {"application", "A"}}},
        /* Verbs that answer without ever writing. They are held to the same
         * property, but their row asserts silence rather than a write, so they
         * cannot make the guard below look satisfied. */
        {"noUpdateProcessing",
         [](FakeUBootEnv &, updater::Bootstate &b) { (void)b.noUpdateProcessing(); },
         false,
         {{"update_reboot_state", "0"}}},
        {"firmware_reboot",
         [](FakeUBootEnv &, updater::Bootstate &b) { (void)b.firmware_reboot(); },
         false,
         {{"update_reboot_state", "7"},
          {"BOOT_ORDER", "B A"},
          {"BOOT_ORDER_OLD", "A B"},
          {"BOOT_B_LEFT", "2"},
          {"rauc_cmd", "rauc.slot=B"}}},
        {"pendingUpdateRollback",
         [&nothing_mounted](FakeUBootEnv &e, updater::Bootstate &b) {
             auto state = update_definitions::read_update_reboot_state(e);
             (void)b.pendingUpdateRollback(state, nothing_mounted.root());
         },
         false,
         {{"update_reboot_state", "7"},
          {"BOOT_ORDER", "B A"},
          {"BOOT_ORDER_OLD", "A B"},
          {"BOOT_B_LEFT", "0"},
          {"rauc_cmd", "rauc.slot=A"}}},
    };

    for (VerbCase &verb : rollback_verbs(app_slot_mounted))
    {
        verbs.push_back(verb);
    }
    return verbs;
}

/* Non-vacuity, verb by verb. A verb that must write and stops writing fails
 * here, instead of quietly turning the property below into a statement about
 * nothing. A summed count over the whole matrix cannot do this: a few writing
 * verbs would keep it above zero while the rest had gone silent. */
TEST_F(BootstateFixture, EveryVerbStillActsOnTheShapeItOwns)
{
    FakeSysfsBlockRoot app_slot_mounted({{"loop0", "/data/app/images/app_b.squashfs"}});
    FakeSysfsBlockRoot nothing_mounted({});

    for (const VerbCase &verb : every_verb(app_slot_mounted, nothing_mounted))
    {
        SCOPED_TRACE(verb.name);
        auto env = make_env(verb.acting_env);
        auto bootstate = make_bootstate(env);

        /* Deliberately not swallowed: a shape on which the verb refuses would
         * write nothing and reintroduce exactly the vacuity this pins. */
        ASSERT_NO_THROW(verb.run(*env, *bootstate));
        env->flushEnvironment();

        const std::vector<std::string> written = env->writes_of("update_reboot_state");
        EXPECT_EQ(!written.empty(), verb.stages_reboot_state);
        for (const std::string &value : written)
        {
            EXPECT_TRUE(inside_alphabet(value)) << "wrote \"" << value << "\"";
        }
    }
}

/* Whatever a verb does with an uninterpretable environment -- act or refuse --
 * it must never persist a value outside the alphabet. A stored out-of-alphabet
 * value would make every read on an older image fail after a fallback onto it,
 * so this holds for the write attempt, not only for the surviving value. Each
 * verb is driven on its own acting shape, so the corrupt seed travels the same
 * path that writes when the seed is readable. */
TEST_F(BootstateFixture, NoVerbStagesAnOutOfAlphabetRebootState)
{
    FakeSysfsBlockRoot app_slot_mounted({{"loop0", "/data/app/images/app_b.squashfs"}});
    FakeSysfsBlockRoot nothing_mounted({});

    for (const VerbCase &verb : every_verb(app_slot_mounted, nothing_mounted))
    {
        for (const RebootStateSeed &seed : uninterpretable_seeds())
        {
            SCOPED_TRACE(std::string(verb.name) + " / " + seed.label);
            auto env = make_env(verb.acting_env);
            seed_reboot_state(*env, seed);
            auto bootstate = make_bootstate(env);

            try
            {
                verb.run(*env, *bootstate);
            }
            catch (...)
            {
                /* Refusing is allowed. Writing a value nobody can read is not. */
            }
            env->flushEnvironment();

            for (const std::string &written : env->writes_of("update_reboot_state"))
            {
                EXPECT_NE(written, "13");
                EXPECT_TRUE(inside_alphabet(written)) << "wrote \"" << written << "\"";
            }
            if (env->holds("update_reboot_state") && !env->writes_of("update_reboot_state").empty())
            {
                EXPECT_TRUE(inside_alphabet(env->at("update_reboot_state")))
                    << "left \"" << env->at("update_reboot_state") << "\"";
            }
        }
    }
}

/* --- firmware_reboot answers from boot evidence (pinned, not endorsed) --- */

/* With an uninterpretable
 * marker the two rollback-pending branches are skipped and the verdict comes
 * from the boot order and the boot budgets alone, so a raw library caller
 * that previously got a throw now gets COMPLETE or PENDING. Both answers
 * match what the same environment yields with a readable marker. */
TEST_F(BootstateFixture, FirmwareRebootAnswersFromBootEvidenceWhenStateUninterpretable)
{
    {
        auto env = make_env({{"BOOT_ORDER", "B A"},
                             {"BOOT_ORDER_OLD", "A B"},
                             {"BOOT_B_LEFT", "2"},
                             {"rauc_cmd", "rauc.slot=B"}});
        env->set("update_reboot_state", "13");
        auto bootstate = make_bootstate(env);

        EXPECT_TRUE(bootstate->firmware_reboot());
        EXPECT_TRUE(env->writes_of("update_reboot_state").empty());
    }
    {
        auto env = make_env({{"BOOT_ORDER", "B A"}, {"BOOT_ORDER_OLD", "A B"}, {"rauc_cmd", "rauc.slot=A"}});
        env->set("update_reboot_state", "not-a-state");
        auto bootstate = make_bootstate(env);

        EXPECT_FALSE(bootstate->firmware_reboot());
        EXPECT_TRUE(env->writes_of("update_reboot_state").empty());
    }
}

/* --- exit coverage for the app-rollback-pending durable state --- */

struct AppRollbackExitCase
{
    const char *name;
    std::map<std::string, std::string> loop_devices;
    /* What the classification answers for this shape. */
    updater::Bootstate::AppRollbackOutcome outcome;
    /* true: the commit's precondition holds and it finalises the rollback.
       false: the reboot is what leads out, and the commit refuses until then. */
    bool commit_settles;
    const char *note;
};

/* Which mounted-image shapes have a verb that leads out of the app-rollback
 * pending state, and which verb it is. Every shape needs one: a value the
 * environment can legally hold with no verb that settles it is a trap,
 * whatever its likelihood, and the third row here was exactly that until the
 * commit learned to accept an unmountable image as evidence. */
std::vector<AppRollbackExitCase> app_rollback_exit_cases()
{
    return {
        {"active_slot_mounted",
         {{"loop0", "/data/app/images/app_b.squashfs"}},
         updater::Bootstate::AppRollbackOutcome::COMMIT_REQUESTED,
         true,
         "rollback pending: the commit path settles it"},
        {"other_slot_mounted",
         {{"loop0", "/data/app/images/app_a.squashfs"}},
         updater::Bootstate::AppRollbackOutcome::REBOOT_OUTSTANDING,
         false,
         "reboot still outstanding: the reboot leads out"},
        {"nothing_mounted",
         {},
         updater::Bootstate::AppRollbackOutcome::INDETERMINATE,
         true,
         "the slot switch already happened and no boot changes it back, so nothing is left "
         "to validate; the commit settles it on that evidence (library issue 53)"},
    };
}

class AppRollbackExit : public BootstateFixture, public ::testing::WithParamInterface<AppRollbackExitCase>
{
};

TEST_P(AppRollbackExit, HasAVerbThatLeadsOut)
{
    const AppRollbackExitCase &row = GetParam();

    FakeSysfsBlockRoot sysfs(row.loop_devices);
    /* Bitfield settled, so the answer comes from the loop-device probe rather
     * than from the uncommitted digit -- that probe is where the row differs. */
    auto env = make_env({{"update_reboot_state", "8"}, {"update", "0000"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);
    auto state = update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING;

    /* Raising is the failure this row family exists to catch: a probe that
     * throws leaves the state with no verb at all. Asserting the verdict on
     * top of that is what tells the two exits apart -- commit or reboot. */
    bool settles = false;
    ASSERT_NO_THROW(settles = bootstate->pendingUpdateRollback(state, sysfs.root())) << row.note;
    EXPECT_EQ(settles, row.commit_settles) << row.note;

    /* The structural half: the reported classification and the commit's
     * precondition are one derivation, not two that happen to agree today.
     * Only an outstanding reboot leads out without a commit -- assert that as
     * a relation, so a future arm cannot report one thing and gate another. */
    updater::Bootstate::AppRollbackOutcome outcome{};
    ASSERT_NO_THROW(outcome = bootstate->classify_app_rollback(sysfs.root())) << row.note;
    EXPECT_EQ(outcome, row.outcome) << row.note;
    EXPECT_EQ(settles, outcome != updater::Bootstate::AppRollbackOutcome::REBOOT_OUTSTANDING)
        << "the reported outcome and the commit precondition disagree: " << row.note;
}

INSTANTIATE_TEST_SUITE_P(MountedImageShapes, AppRollbackExit, ::testing::ValuesIn(app_rollback_exit_cases()),
                         [](const ::testing::TestParamInfo<AppRollbackExitCase> &info) {
                             return std::string(info.param.name);
                         });

/* --- the classification's own edges --- */

TEST_F(BootstateFixture, ClassifyAppRollbackBitfieldDecidesBeforeTheProbe)
{
    /* A running application slot still marked uncommitted is the rollback's
     * own durable record. It has to be enough on its own: pointing the probe
     * at a root that does not exist would raise, so reaching this answer
     * without a throw is what proves the bitfield was consulted first. */
    auto env = make_env({{"update_reboot_state", "8"}, {"update", "0001"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    updater::Bootstate::AppRollbackOutcome outcome{};
    ASSERT_NO_THROW(outcome = bootstate->classify_app_rollback("/nonexistent/sysfs-block-root"));
    EXPECT_EQ(outcome, updater::Bootstate::AppRollbackOutcome::COMMIT_REQUESTED);
}

TEST_F(BootstateFixture, ClassifyAppRollbackRaisesWhenTheEvidenceCannotBeRead)
{
    /* Unreadable is not the same as empty: zero loop devices is an answer
     * (nothing is mounted), an unreadable root is the absence of one. The
     * caller maps both to the same code, but only because it decided to --
     * the library must keep them apart. */
    auto env = make_env({{"update_reboot_state", "8"}, {"update", "0000"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    EXPECT_THROW(bootstate->classify_app_rollback("/nonexistent/sysfs-block-root"), updater::GetLoopDevices);
}

TEST_F(BootstateFixture, ClassifyAppRollbackWritesNothingInAnyShape)
{
    /* It sits inside the commit's open transaction, so a write here would
     * reach the environment on a read. Checked against the write journal
     * rather than against "it did not throw". */
    const std::vector<std::map<std::string, std::string>> shapes = {
        {{"loop0", "/data/app/images/app_b.squashfs"}},
        {{"loop0", "/data/app/images/app_a.squashfs"}},
        {},
    };

    for (const auto &loop_devices : shapes)
    {
        FakeSysfsBlockRoot sysfs(loop_devices);
        auto env = make_env({{"update_reboot_state", "8"}, {"update", "0000"}, {"application", "B"}});
        auto bootstate = make_bootstate(env);

        ASSERT_NO_THROW((void)bootstate->classify_app_rollback(sysfs.root()));
        EXPECT_TRUE(env->writes_of("update_reboot_state").empty());
        EXPECT_TRUE(env->writes_of("update").empty());
        EXPECT_TRUE(env->writes_of("application").empty());
    }
}


/* --- how the commit paths read a digit: characterization, not intent ---
 *
 * The bitfield digit carries two independent facts, "this slot is trusted"
 * (bad bit) and "an update is in flight on it" (uncommitted bit). Most of the
 * commit paths compare the digit as a character rather than testing the bit
 * they mean. The four cases below pin what that does today, so that unifying
 * the readers flips something visible instead of passing silently. None of
 * them asserts that the current answer is the right one.
 */

/* The settle step clears the in-flight bit and nothing else. A digit that also
 * carries a bad mark keeps it: the rollback establishes that this slot is no
 * longer being updated, not that it is trustworthy again. */
TEST_F(BootstateFixture, AppRollbackCommitSettlesOnlyTheInFlightBitAndKeepsTheBadMark)
{
    auto env = make_env({{"update_reboot_state", "8"}, {"update", "0003"}, {"application", "A"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0002");
    EXPECT_EQ(env->at("update_reboot_state"), "0");
}

/* The same settle step on the plain in-flight digit, as the contrast that
 * makes the case above mean something: here the character does match and the
 * slot is settled. */
TEST_F(BootstateFixture, AppRollbackCommitSettlesAPlainInFlightDigit)
{
    auto env = make_env({{"update_reboot_state", "8"}, {"update", "0001"}, {"application", "A"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0000");
}

/* A bad mark is a verdict about a slot and survives a rollback: only an
 * install that replaces the payload clears it. The branch therefore has to ask
 * whether the target is in flight, not whether its digit is exactly committed
 * -- otherwise a slot that was marked bad is silently absolved. */
TEST_F(BootstateFixture, FwRollbackCommitKeepsABadMarkOnTheTargetSlot)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"update", "0020"},
                         {"BOOT_ORDER", "A B"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"rauc_cmd", "rauc.slot=A"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0020");
}

/* Same shape, digit carrying both bits: the in-flight bit goes, the verdict
 * stays. */
TEST_F(BootstateFixture, FwRollbackCommitSettlesOnlyTheInFlightBitOfADigitCarryingBoth)
{
    auto env = make_env({{"update_reboot_state", "7"},
                         {"update", "0030"},
                         {"BOOT_ORDER", "A B"},
                         {"BOOT_ORDER_OLD", "A B"},
                         {"rauc_cmd", "rauc.slot=A"}});
    auto bootstate = make_bootstate(env);

    bootstate->confirmUpdateRollback();
    env->flushEnvironment();

    EXPECT_EQ(env->at("update"), "0020");
}

} // namespace
