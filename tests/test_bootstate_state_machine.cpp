#include <gtest/gtest.h>

#include "handle_update/handleUpdate.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "uboot_interface/IUBootEnv.h"
#include "util/posix_utils.h"

extern "C" {
#include <unistd.h>
}

#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

/* In-memory U-Boot environment. Mirrors the concrete UBoot conversion and
 * allowed-list semantics closely enough for state-machine tests: unknown keys
 * and values outside the allowed list throw, like the real accessors.
 * Deferred-write: addVariable() stages into a pending map, invisible to
 * getVariable() until flushEnvironment() — matching real UBoot::UBoot, where
 * a read never sees an unflushed write. This matters for order-sensitive
 * rollback logic that re-reads a variable it just staged. */
class FakeUBootEnv : public UBoot::IUBootEnv
{
  public:
    explicit FakeUBootEnv(std::map<std::string, std::string> seed) : env_(std::move(seed)) {}

    void addVariable(const std::string &key, const std::string &value) override
    {
        staged_[key] = value;
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
        const int value = std::stoi(raw);
        for (const uint8_t candidate : allowed)
        {
            if (candidate == value)
            {
                return candidate;
            }
        }
        throw std::runtime_error("not allowed content: " + name + "=" + raw);
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
        throw std::runtime_error("not allowed content: " + name + "=" + raw);
    }

    char getVariable(const std::string &name, const std::vector<char> &allowed) override
    {
        const std::string raw = fetch(name);
        if (raw.size() == 1)
        {
            for (const char candidate : allowed)
            {
                if (candidate == raw.front())
                {
                    return candidate;
                }
            }
        }
        throw std::runtime_error("not allowed content: " + name + "=" + raw);
    }

    std::string getVariable(const std::string &name, bool (*validator)(const std::string &)) override
    {
        const std::string raw = fetch(name);
        if (!validator(raw))
        {
            throw std::runtime_error("validator rejected: " + name + "=" + raw);
        }
        return raw;
    }

    const std::string &at(const std::string &name) const
    {
        return env_.at(name);
    }

  private:
    std::string fetch(const std::string &name) const
    {
        const auto it = env_.find(name);
        if (it == env_.end())
        {
            throw std::runtime_error("no such variable: " + name);
        }
        return it->second;
    }

    std::map<std::string, std::string> env_;
    std::map<std::string, std::string> staged_;
};

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

TEST_F(BootstateFixture, PendingUpdateRollbackThrowsWhenNothingMounted)
{
    /* Post-mount actor path: zero loop devices while an app rollback awaits
     * its reboot means a real fault, not the pre-mount state. */
    FakeSysfsBlockRoot sysfs({});
    auto env = make_env({{"update_reboot_state", "8"}, {"application", "B"}});
    auto bootstate = make_bootstate(env);

    auto state = update_definitions::UBootBootstateFlags::ROLLBACK_APP_REBOOT_PENDING;
    EXPECT_THROW((void)bootstate->pendingUpdateRollback(state, sysfs.root()), updater::GetLoopDevices);
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

} // namespace
