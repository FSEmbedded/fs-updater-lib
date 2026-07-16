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

    /* The proven slot stays preferred and the slot that failed its switch
     * boot is recorded as bad — not re-armed with a fresh boot budget. */
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

    EXPECT_TRUE(bootstate->application_reboot(sysfs.root()));
}

TEST_F(BootstateFixture, ApplicationRebootFalseWhenNoLoopDeviceMatchesExpectedSlot)
{
    FakeSysfsBlockRoot sysfs({{"loop0", "/some/other/image.squashfs"}});
    auto env = make_env({{"application", "A"}});
    auto bootstate = make_bootstate(env);

    EXPECT_FALSE(bootstate->application_reboot(sysfs.root()));
}

} // namespace
