#include <gtest/gtest.h>

#include "fake_env.h"
#include "handle_update/fs_exceptions.h"
#include "handle_update/handleUpdate.h"
#include "handle_update/updateBase.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "uboot_interface/UBoot.h"
#include "uboot_interface/allowed_uboot_variable_states.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>

// Drives the real Bootstate and UBoot::UBoot against the in-memory libuboot
// stub. FSUpdate itself is not host-buildable (its update paths construct
// applicationUpdate, which reads the device's RAUC configuration, and link
// libarchive); the few environment writes FSUpdate makes around the Bootstate
// calls are mirrored by the fixture helpers and marked as such.

namespace
{

using update_definitions::Flags;
using update_definitions::UBootBootstateFlags;

constexpr char APP_A_IMAGE[] = "/rw_fs/root/application/app_a.squashfs";
constexpr char APP_B_IMAGE[] = "/rw_fs/root/application/app_b.squashfs";

// Stands in for applicationUpdate, whose constructor needs the device's RAUC
// configuration. rollback() does what applicationUpdate::rollback() does:
// stage the other slot in "application" without flushing.
class FakeApplicationUpdater : public updater::updateBase
{
public:
    using updater::updateBase::updateBase;

    void install(const std::string &) override {}

    void rollback() override
    {
        const char current = uboot_handler->getVariable("application", allowed_application_variables);
        uboot_handler->addVariable("application", current == 'A' ? "B" : "A");
    }

    version_t getCurrentVersion() override { return version_t{}; }
};

class BootstateTest : public ::testing::Test
{
protected:
    std::filesystem::path backing_file;
    std::filesystem::path marker;
    std::shared_ptr<UBoot::UBoot> uboot;
    std::unique_ptr<updater::Bootstate> bootstate;
    std::unique_ptr<FakeApplicationUpdater> app_updater;

    static std::shared_ptr<logger::LoggerHandler> logger()
    {
        static std::shared_ptr<logger::LoggerHandler> handler =
            logger::LoggerHandler::initLogger(std::make_shared<logger::LoggerSinkEmpty>(logger::logLevel::ERROR));
        return handler;
    }

    void SetUp() override
    {
        backing_file = std::filesystem::temp_directory_path() / ("fsup_backing_file_" + std::to_string(::getpid()));
        // The work directory the test binary was compiled with.
        std::filesystem::create_directories(TEMP_ADU_WORK_DIR);
        marker = std::filesystem::path(TEMP_ADU_WORK_DIR) / "updateInstalled";
        std::filesystem::remove(marker);
        // Running slot B with application B; slot A carries an abandoned
        // firmware and is marked bad.
        fake_env::reset({{"rauc_cmd", "rauc.slot=B"},
                         {"BOOT_ORDER", "B A"},
                         {"BOOT_ORDER_OLD", "B A"},
                         {"BOOT_A_LEFT", "3"},
                         {"BOOT_B_LEFT", "3"},
                         {"application", "B"},
                         {"update", "2000"},
                         {"update_reboot_state", "0"}});
        mount(APP_B_IMAGE);
        uboot = std::make_shared<UBoot::UBoot>("unused");
        bootstate = std::make_unique<updater::Bootstate>(uboot, logger(), backing_file.string());
        app_updater = std::make_unique<FakeApplicationUpdater>(uboot, logger());
    }

    void TearDown() override
    {
        std::filesystem::remove(backing_file);
        std::filesystem::remove(marker);
    }

    std::string env(const std::string &name) const { return fake_env::flash().at(name); }
    void set(const std::string &name, const std::string &value) { fake_env::flash()[name] = value; }
    std::map<std::string, std::string> snapshot() const { return fake_env::flash(); }

    // The loop device's backing file names the mounted application image.
    void mount(const std::string &image) { std::ofstream(backing_file) << image << '\n'; }
    void unmount() { std::filesystem::remove(backing_file); }

    // FSUpdate::update_image writes the marker into the volatile work
    // directory once an install has completed; a reboot removes it.
    void mark_installed() { std::ofstream(marker) << ""; }
    bool installed() const { return std::filesystem::exists(marker); }

    std::string running_slot() const { return env("rauc_cmd").substr(env("rauc_cmd").find('=') + 1); }
    static std::string other(const std::string &slot) { return slot == "A" ? "B" : "A"; }

    // Boot the first slot of BOOT_ORDER that still has attempts left, which
    // costs it one attempt, clear the volatile work directory and mount the
    // application slot named by "application".
    void reboot()
    {
        std::string slot = env("BOOT_ORDER").substr(0, 1);
        if (env("BOOT_" + slot + "_LEFT") == "0") {
            slot = other(slot);
        }
        set("BOOT_" + slot + "_LEFT", std::string(1, env("BOOT_" + slot + "_LEFT").at(0) - 1));
        set("rauc_cmd", "rauc.slot=" + slot);
        std::filesystem::remove(marker);
        mount(env("application") == "A" ? APP_A_IMAGE : APP_B_IMAGE);
    }

    // FSUpdate::update_application: the pre-write before applicationUpdate::install().
    // An install that is stopped while writing leaves exactly this behind.
    void interrupt_application_install()
    {
        UBoot::UBoot::EnvTransaction txn(*uboot);
        std::string update = env("update");
        update.at(bootstate->get_update_bit(Flags::APP, true)) = '1';
        uboot->addVariable("update", update);
        uboot->addVariable("update_reboot_state",
                           update_definitions::to_string(UBootBootstateFlags::INCOMPLETE_APP_UPDATE));
        uboot->flushEnvironment();
    }

    // The pre-write, what a completed install() flushes (the target slot in
    // "application"), then the marker.
    void install_application()
    {
        interrupt_application_install();
        uboot->addVariable("application", other(env("application")));
        uboot->flushEnvironment();
        mark_installed();
    }

    // FSUpdate::update_firmware: the pre-write before firmwareUpdate::install(),
    // then what the installer leaves behind: the target slot first in BOOT_ORDER.
    void install_firmware(UBootBootstateFlags state = UBootBootstateFlags::INCOMPLETE_FW_UPDATE)
    {
        {
            UBoot::UBoot::EnvTransaction txn(*uboot);
            std::string update = env("update");
            update.at(bootstate->get_update_bit(Flags::OS, true)) = '1';
            uboot->addVariable("update", update);
            uboot->addVariable("update_reboot_state", update_definitions::to_string(state));
            uboot->flushEnvironment();
        }
        const std::string target = other(running_slot());
        set("BOOT_ORDER_OLD", env("BOOT_ORDER"));
        set("BOOT_ORDER", target + " " + running_slot());
        set("BOOT_" + target + "_LEFT", "3");
        mark_installed();
    }

    // FSUpdate::update_firmware_and_application stopped after the application
    // pre-write: firmware first, then the application digit and the combined
    // state. install() never named the written slot, so "application" is
    // unchanged and no marker exists.
    void interrupt_firmware_and_application_install()
    {
        install_firmware(UBootBootstateFlags::INCOMPLETE_FW_UPDATE);
        std::filesystem::remove(marker);
        UBoot::UBoot::EnvTransaction txn(*uboot);
        std::string update = env("update");
        update.at(bootstate->get_update_bit(Flags::APP, true)) = '1';
        uboot->addVariable("update", update);
        uboot->addVariable("update_reboot_state",
                           update_definitions::to_string(UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE));
        uboot->flushEnvironment();
    }

    // The same, completed: install() names the written slot, then the marker.
    void install_firmware_and_application()
    {
        interrupt_firmware_and_application_install();
        uboot->addVariable("application", other(env("application")));
        uboot->flushEnvironment();
        mark_installed();
    }

    // FSUpdate::commit_update, reduced to the dispatch the tests reach.
    bool commit()
    {
        UBoot::UBoot::EnvTransaction txn(*uboot);
        bool committed = false;
        if (bootstate->pendingApplicationUpdate()) {
            bootstate->confirmPendingApplicationUpdate();
            committed = true;
        } else if (bootstate->pendingFirmwareUpdate()) {
            bootstate->confirmPendingFirmwareUpdate();
            committed = true;
        } else if (bootstate->pendingApplicationFirmwareUpdate()) {
            bootstate->confirmPendingApplicationFirmwareUpdate();
            committed = true;
        } else if (!bootstate->noUpdateProcessing()) {
            UBootBootstateFlags state = update_definitions::to_UBootBootstateFlags(
                uboot->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
            if (!bootstate->pendingUpdateRollback(state)) {
                throw fs::NotAllowedUpdateState();
            }
            bootstate->confirmUpdateRollback();
            committed = true;
        }
        uboot->flushEnvironment();
        return committed;
    }

    // FSUpdate::rollback_application for a pending application update.
    void rollback_application()
    {
        UBoot::UBoot::EnvTransaction txn(*uboot);
        bootstate->refuse_rollback_before_reboot();
        ASSERT_TRUE(bootstate->pendingApplicationUpdate());
        bootstate->applicaton_rollback(*app_updater);
        uboot->flushEnvironment();
    }

    // FSUpdate::rollback_firmware: the reboot guard, then either the pending
    // firmware update or the switch between two committed firmware slots.
    // Both branches are modelled because which one runs is what the state of
    // the update digits decides.
    void rollback_firmware()
    {
        UBoot::UBoot::EnvTransaction txn(*uboot);
        bootstate->refuse_rollback_before_reboot();
        if (bootstate->pendingFirmwareUpdate() || bootstate->pendingApplicationFirmwareUpdate()) {
            bootstate->firmware_rollback();
        } else {
            UBootBootstateFlags state = update_definitions::to_UBootBootstateFlags(
                uboot->getVariable("update_reboot_state", allowed_update_reboot_state_variables));
            if (bootstate->rollbackInProgress(state)) {
                throw fs::GenericException("Commit for rollback required");
            }
            const int next_update_state =
                env("update").at(running_slot() == "A" ? FIRMWARE_B_INDEX : FIRMWARE_A_INDEX) - '0';
            if ((next_update_state & STATE_UPDATE_UNCOMMITED) == STATE_UPDATE_UNCOMMITED) {
                throw fs::GenericException("Firmware rollback is not allowed.", ECANCELED);
            }
            if ((next_update_state & STATE_UPDATE_BAD) == STATE_UPDATE_BAD) {
                throw fs::GenericException("Firmware rollback is not allowed.", EPERM);
            }
            uboot->addVariable("BOOT_ORDER", other(running_slot()) + " " + running_slot());
            uboot->addVariable("BOOT_ORDER_OLD", running_slot() + " " + other(running_slot()));
            uboot->addVariable("update_reboot_state",
                               update_definitions::to_string(UBootBootstateFlags::ROLLBACK_FW_REBOOT_PENDING));
        }
        uboot->flushEnvironment();
    }

    // The command line's rollback of a pending combined update: first
    // FSUpdate::rollback_application, which stages the application part but
    // flushes only for an application-only update, so its transaction closes
    // and discards it; then FSUpdate::rollback_firmware, which does both parts.
    void rollback_firmware_and_application()
    {
        {
            UBoot::UBoot::EnvTransaction txn(*uboot);
            bootstate->refuse_rollback_before_reboot();
            ASSERT_TRUE(bootstate->pendingApplicationFirmwareUpdate());
            bootstate->applicaton_rollback(*app_updater);
        }
        UBoot::UBoot::EnvTransaction txn(*uboot);
        bootstate->refuse_rollback_before_reboot();
        bootstate->firmware_rollback();
        app_updater->rollback();
        uboot->addVariable("update_reboot_state",
                           update_definitions::to_string(UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING));
        uboot->flushEnvironment();
    }
};

// --- digit index table ------------------------------------------------------

TEST_F(BootstateTest, UpdateBitIndexFollowsRunningSlotAndApplication)
{
    EXPECT_EQ(bootstate->get_update_bit(Flags::OS, false), FIRMWARE_B_INDEX);
    EXPECT_EQ(bootstate->get_update_bit(Flags::OS, true), FIRMWARE_A_INDEX);
    EXPECT_EQ(bootstate->get_update_bit(Flags::APP, false), APPLICATION_B_INDEX);
    EXPECT_EQ(bootstate->get_update_bit(Flags::APP, true), APPLICATION_A_INDEX);

    set("rauc_cmd", "rauc.slot=A");
    set("application", "A");
    EXPECT_EQ(bootstate->get_update_bit(Flags::OS, false), FIRMWARE_A_INDEX);
    EXPECT_EQ(bootstate->get_update_bit(Flags::OS, true), FIRMWARE_B_INDEX);
    EXPECT_EQ(bootstate->get_update_bit(Flags::APP, false), APPLICATION_A_INDEX);
    EXPECT_EQ(bootstate->get_update_bit(Flags::APP, true), APPLICATION_B_INDEX);
}

// --- invalid "update" contents are refused on every read ----------------------

TEST_F(BootstateTest, RejectsTwoUncommittedApplicationDigits)
{
    set("update", "2101");
    EXPECT_THROW(bootstate->pendingApplicationUpdate(), UBoot::UBootEnvVarNotAllowedContent);
}

TEST_F(BootstateTest, RejectsTwoUncommittedFirmwareDigits)
{
    set("update", "1010");
    EXPECT_THROW(bootstate->pendingFirmwareUpdate(), UBoot::UBootEnvVarNotAllowedContent);
}

TEST_F(BootstateTest, RejectsDigitOutOfRangeAndWrongLength)
{
    set("update", "4000");
    EXPECT_THROW(bootstate->pendingFirmwareUpdate(), UBoot::UBootEnvVarNotAllowedContent);
    set("update", "200");
    EXPECT_THROW(bootstate->pendingFirmwareUpdate(), UBoot::UBootEnvVarNotAllowedContent);
}

// --- application install and the paths that already worked ------------------

TEST_F(BootstateTest, ApplicationInstallFlagsTheTargetSlot)
{
    install_application();

    EXPECT_EQ(env("update"), "2100");
    EXPECT_EQ(env("application"), "A");
    EXPECT_EQ(env("update_reboot_state"), "3");
    EXPECT_TRUE(bootstate->pendingApplicationUpdate());
    EXPECT_FALSE(bootstate->application_reboot());
}

TEST_F(BootstateTest, ApplicationCommitAfterRebootClearsTheInstalledDigit)
{
    install_application();
    reboot();
    ASSERT_TRUE(bootstate->application_reboot());

    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("application"), "A");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

TEST_F(BootstateTest, ApplicationCommitWithoutRebootIsRefused)
{
    install_application();
    EXPECT_THROW(commit(), updater::MissingReboot);
    EXPECT_EQ(env("update"), "2100");
}

TEST_F(BootstateTest, ApplicationRollbackAfterRebootMarksTheSlotBadOnCommit)
{
    install_application();
    reboot();
    rollback_application();

    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update"), "2100");
    EXPECT_EQ(env("update_reboot_state"), "8");

    reboot();
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2200");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

TEST_F(BootstateTest, ApplicationRebootNeedsTheLoopDevice)
{
    unmount();
    EXPECT_THROW(bootstate->application_reboot(), updater::GetLoopDevices);
}

// With no loop device at all (nothing mounted, the backing file is missing,
// unlike an empty one) neither the rollback nor the commit of a pending
// application update can read the state: both fail on the loop device and
// change nothing. Pins what is, not what should be.
TEST_F(BootstateTest, ApplicationRollbackAndCommitFailWithoutTheLoopDevice)
{
    install_application();
    reboot();
    unmount();
    const auto before = snapshot();

    EXPECT_THROW(rollback_application(), updater::GetLoopDevices);
    EXPECT_THROW(commit(), updater::GetLoopDevices);
    EXPECT_EQ(snapshot(), before);
}

TEST_F(BootstateTest, InterruptedApplicationInstallRollbackAndCommitFailWithoutTheLoopDevice)
{
    interrupt_application_install();
    reboot();
    unmount();
    const auto before = snapshot();

    EXPECT_THROW(rollback_application(), updater::GetLoopDevices);
    EXPECT_THROW(commit(), updater::GetLoopDevices);
    EXPECT_EQ(snapshot(), before);
}

// --- application rolled back before the reboot ------------------------------

// The marker says nothing was booted since the install: the written slot
// keeps its digit and "application" its new value. A second attempt is
// refused the same way.
TEST_F(BootstateTest, ApplicationRollbackWithoutRebootIsRefused)
{
    install_application();
    ASSERT_TRUE(installed());
    const auto before = snapshot();

    EXPECT_THROW(rollback_application(), updater::MissingReboot);
    EXPECT_THROW(rollback_application(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
    EXPECT_EQ(env("update"), "2100");
    EXPECT_EQ(env("application"), "A");
    EXPECT_EQ(env("update_reboot_state"), "3");
    EXPECT_TRUE(installed());
    EXPECT_TRUE(bootstate->pendingApplicationUpdate());
    EXPECT_FALSE(bootstate->application_reboot());
}

TEST_F(BootstateTest, ApplicationRollbackWithoutRebootIsRefusedFromSlotA)
{
    set("application", "A");
    mount(APP_A_IMAGE);
    install_application();
    ASSERT_EQ(env("update"), "2001");
    const auto before = snapshot();

    EXPECT_THROW(rollback_application(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
    EXPECT_TRUE(bootstate->pendingApplicationUpdate());
}

// An earlier application cycle left the running slot with a spent attempt;
// the counters say nothing about this install.
TEST_F(BootstateTest, ApplicationRollbackWithoutRebootIsRefusedWithASpentAttempt)
{
    set("BOOT_B_LEFT", "2");
    install_application();
    const auto before = snapshot();

    EXPECT_THROW(rollback_application(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
    EXPECT_EQ(env("update"), "2100");
    EXPECT_TRUE(installed());
}

TEST_F(BootstateTest, ApplicationRollbackAfterRefusalAndRebootStillWorks)
{
    install_application();
    EXPECT_THROW(rollback_application(), updater::MissingReboot);

    reboot();
    ASSERT_FALSE(installed());
    ASSERT_TRUE(bootstate->application_reboot());
    rollback_application();
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update_reboot_state"), "8");

    reboot();
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2200");
}

// Full counters after the reboot (a mark-good ran) do not make the rollback
// a refused one once the marker is gone.
TEST_F(BootstateTest, ApplicationRollbackAfterRebootWithFullCountersIsNotRefused)
{
    install_application();
    reboot();
    set("BOOT_B_LEFT", "3");
    ASSERT_FALSE(installed());

    rollback_application();

    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update_reboot_state"), "8");
}

// The reboot happened (the marker is gone) but the mounter did not pick the
// installed image: the only way out is to undo the install in place,
// clearing the digit of the written slot, not the running one.
TEST_F(BootstateTest, ApplicationRollbackRecoversAnImageThatDidNotMount)
{
    install_application();
    reboot();
    mount(APP_B_IMAGE);
    ASSERT_FALSE(installed());
    ASSERT_FALSE(bootstate->application_reboot());

    rollback_application();

    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update_reboot_state"), "0");
    EXPECT_FALSE(bootstate->pendingApplicationUpdate());
    EXPECT_TRUE(bootstate->noUpdateProcessing());
}

// noUpdateProcessing() only reads update_reboot_state, which a prior commit
// can reset to idle while leaving a stray uncommitted digit in "update"
// behind (a commit in state 31 does exactly this, leaving
// update=1000). decorator_update_state() must not treat that as "clean" --
// starting a new install onto the other slot would write a second
// uncommitted digit, which validate_update_bits rejects outright, leaving
// every later read of "update" throwing with no CLI exit at all.
// updateDigitsAllCommitted() is the guard this project verifies here;
// its wiring into decorator_update_state (fsupdate.cpp) is not
// host-buildable and so not compile-verified by this suite.
TEST_F(BootstateTest, UpdateDigitsAllCommittedCatchesAStrayDigitDespiteIdleState)
{
    EXPECT_TRUE(bootstate->noUpdateProcessing());
    EXPECT_TRUE(bootstate->updateDigitsAllCommitted());

    set("update", "1000"); // the leftover: fw_a uncommitted, urs still 0
    set("update_reboot_state", "0");

    EXPECT_TRUE(bootstate->noUpdateProcessing());
    EXPECT_FALSE(bootstate->updateDigitsAllCommitted());

    set("update", "0010"); // same trap on the other fw slot
    EXPECT_FALSE(bootstate->updateDigitsAllCommitted());

    set("update", "0100"); // and on an application slot
    EXPECT_FALSE(bootstate->updateDigitsAllCommitted());

    set("update", "0003"); // bad+uncommitted also counts as uncommitted
    EXPECT_FALSE(bootstate->updateDigitsAllCommitted());

    set("update", "2020"); // bad alone is not uncommitted
    EXPECT_TRUE(bootstate->updateDigitsAllCommitted());
}

// The refusal text has to name the slot and the exit that exists for it: a
// '1' is cleared with --set_*_state_bad, a '3' has no exit at all, and two
// digits (one firmware, one application) can be left over at once.
TEST_F(BootstateTest, UncommittedDigitsHintNamesEverySlotAndItsExit)
{
    const auto has = [](const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; };

    set("update", "0010"); // firmware B, which is the running slot
    std::string hint = bootstate->uncommittedDigitsHint();
    EXPECT_TRUE(has(hint, "firmware slot B (running)")) << hint;
    EXPECT_TRUE(has(hint, "--set_fw_state_bad B")) << hint;
    EXPECT_TRUE(has(hint, "Slot B keeps booting")) << hint;
    EXPECT_TRUE(has(hint, " - a new update is refused until this is resolved")) << hint;

    set("update", "1000"); // firmware A, not running
    hint = bootstate->uncommittedDigitsHint();
    EXPECT_TRUE(has(hint, "firmware slot A (not running)")) << hint;
    EXPECT_TRUE(has(hint, "--set_fw_state_bad A")) << hint;
    EXPECT_FALSE(has(hint, "keeps booting")) << hint;

    set("update", "0100"); // application A, not running
    hint = bootstate->uncommittedDigitsHint();
    EXPECT_TRUE(has(hint, "application slot A (not running)")) << hint;
    EXPECT_TRUE(has(hint, "--set_app_state_bad A")) << hint;

    set("update", "0001"); // application B, running
    hint = bootstate->uncommittedDigitsHint();
    EXPECT_TRUE(has(hint, "application slot B (running)")) << hint;
    EXPECT_TRUE(has(hint, "--set_app_state_bad B")) << hint;

    set("update", "0003"); // bad and uncommitted: no verb changes it
    hint = bootstate->uncommittedDigitsHint();
    EXPECT_TRUE(has(hint, "application slot B is marked bad and uncommitted (3)")) << hint;
    EXPECT_FALSE(has(hint, "clear it with")) << hint;

    set("update", "1100"); // one firmware and one application digit at once
    hint = bootstate->uncommittedDigitsHint();
    EXPECT_TRUE(has(hint, "firmware slot A")) << hint;
    EXPECT_TRUE(has(hint, "; application slot A")) << hint;

    set("rauc_cmd", "rauc.slot=A"); // the same digit on the other running slot
    set("update", "1000");
    hint = bootstate->uncommittedDigitsHint();
    EXPECT_TRUE(has(hint, "firmware slot A (running)")) << hint;
}

TEST_F(BootstateTest, ApplicationRollbackRecoversAnImageThatDidNotMountFromSlotA)
{
    set("application", "A");
    mount(APP_A_IMAGE);
    install_application();
    ASSERT_EQ(env("update"), "2001");
    reboot();
    mount(APP_A_IMAGE);
    ASSERT_FALSE(bootstate->application_reboot());

    rollback_application();

    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("application"), "A");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

// The marker was cleared by hand without a reboot: the state is read as a
// reboot that did not mount the image, and the in-place undo is what runs.
TEST_F(BootstateTest, ApplicationRollbackWithoutMarkerBeforeRebootUndoesInPlace)
{
    install_application();
    std::filesystem::remove(marker);

    rollback_application();

    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

// The install stopped before it named the written slot, and after the reboot
// nothing is mounted at all: the digit is cleared where it sits and
// "application" keeps naming the slot that was running.
TEST_F(BootstateTest, ApplicationRollbackClearsAnInterruptedInstallWithoutFlipping)
{
    interrupt_application_install();
    ASSERT_EQ(env("update"), "2100");
    reboot();
    mount("");
    ASSERT_FALSE(bootstate->application_reboot());

    rollback_application();

    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

// --- the marker cannot be read at all -----------------------------------------

// A marker path that leads through a regular file cannot be stat()ed for a
// reason other than absence. That is not a reboot: the rollback refuses.
// The work directory itself is replaced by a regular file for these tests.
class UnreadableMarkerTest : public BootstateTest
{
protected:
    void make_work_dir_unreadable()
    {
        std::filesystem::remove_all(TEMP_ADU_WORK_DIR);
        std::ofstream(TEMP_ADU_WORK_DIR) << "";
    }

    void TearDown() override
    {
        std::filesystem::remove(TEMP_ADU_WORK_DIR);
        BootstateTest::TearDown();
    }
};

TEST_F(UnreadableMarkerTest, ApplicationRollbackIsRefused)
{
    install_application();
    make_work_dir_unreadable();
    const auto before = snapshot();

    EXPECT_THROW(rollback_application(), updater::MissingReboot);
    EXPECT_EQ(snapshot(), before);
}

// With a spent attempt the boot-order predicate says nothing, so only the
// marker's reading decides.
TEST_F(UnreadableMarkerTest, FirmwareRollbackIsRefused)
{
    set("BOOT_B_LEFT", "2");
    install_firmware();
    make_work_dir_unreadable();
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);
    EXPECT_EQ(snapshot(), before);
}

// --- application install stopped before it named the written slot ------------

// "application" still names the running slot and its image is mounted, so
// the state reads as rebooted; the digit sits on the other slot. A rollback
// would flip to that slot. The commit clears the digit, so that is the exit.
TEST_F(BootstateTest, InterruptedApplicationInstallRollbackIsRefused)
{
    interrupt_application_install();
    ASSERT_EQ(env("update"), "2100");
    ASSERT_EQ(env("application"), "B");
    ASSERT_FALSE(installed());
    ASSERT_TRUE(bootstate->pendingApplicationUpdate());
    ASSERT_TRUE(bootstate->application_reboot());
    const auto before = snapshot();

    EXPECT_THROW(rollback_application(), updater::CommitRequired);
    EXPECT_THROW(rollback_application(), updater::CommitRequired);

    EXPECT_EQ(snapshot(), before);
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

TEST_F(BootstateTest, InterruptedApplicationInstallRollbackIsRefusedAfterReboot)
{
    interrupt_application_install();
    reboot();
    const auto before = snapshot();

    EXPECT_THROW(rollback_application(), updater::CommitRequired);

    EXPECT_EQ(snapshot(), before);
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("application"), "B");
}

// --- firmware install and the paths that already worked ---------------------

TEST_F(BootstateTest, FirmwareInstallFlagsTheTargetSlot)
{
    install_firmware();

    EXPECT_EQ(env("update"), "1000");
    EXPECT_EQ(env("BOOT_ORDER"), "A B");
    EXPECT_EQ(env("BOOT_ORDER_OLD"), "B A");
    EXPECT_TRUE(bootstate->pendingFirmwareUpdate());
    EXPECT_FALSE(bootstate->firmware_reboot());
}

TEST_F(BootstateTest, FirmwareCommitAfterSuccessfulRebootClearsTheDigit)
{
    install_firmware();
    reboot();
    ASSERT_EQ(running_slot(), "A");

    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "0000");
    EXPECT_EQ(env("BOOT_ORDER_OLD"), "A B");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

TEST_F(BootstateTest, FirmwareCommitAfterFailedRebootMarksTheSlotBad)
{
    install_firmware();
    set("BOOT_A_LEFT", "0");
    reboot();
    ASSERT_EQ(running_slot(), "B");

    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_EQ(env("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

TEST_F(BootstateTest, FirmwareCommitWithoutRebootIsRefused)
{
    install_firmware();
    EXPECT_THROW(commit(), updater::MissingReboot);
    EXPECT_EQ(env("update"), "1000");
}

// pendingUpdateRollback()'s urs==7 branch short-circuits on the OS digit's
// mere presence, before ever asking pendingFirmwareRollback() whether the
// rollback's own reboot happened. That digit is set at install time and
// stays set across firmware_rollback()'s BOOT_<slot>_LEFT=0 trigger (it
// never touches "update"), so the digit is present in exactly this
// pre-reboot window too -- not just after it, which the check's shape
// assumes. Measured on the board: --commit_update right after
// --rollback_update (skipping the required reboot) answers rc 16 and
// silently keeps the stray digit forever, because confirmUpdateRollback()
// then also reads the wrong (untouched) slot's digit.
TEST_F(BootstateTest, FirmwareCommitBeforeTheRollbacksOwnRebootIsRefused)
{
    install_firmware();
    reboot();
    rollback_firmware();
    ASSERT_EQ(env("BOOT_A_LEFT"), "0");
    ASSERT_EQ(env("update_reboot_state"), "7");
    ASSERT_EQ(running_slot(), "A");
    const auto before = snapshot();

    EXPECT_THROW(commit(), fs::NotAllowedUpdateState);

    EXPECT_EQ(snapshot(), before);
}

// rollback_firmware()'s own switch-between-committed-slots guard used to
// share pendingUpdateRollback() with commit_update() -- once that was fixed
// to answer the reboot-aware question for the commit case, the same guard
// stopped refusing a second --switch_fw_slot/--rollback_update while a case
// 1 rollback (this test's own first rollback_firmware() call) was still in
// its pre-reboot window, letting it proceed into a fresh committed-slot
// switch and lose the first rollback's outcome. rollbackInProgress() is the
// urs-only check the guard actually needs; pendingFirmwareUpdate() is false
// by the second call (urs is 7, not 2), so this exercises the guard branch.
TEST_F(BootstateTest, SwitchGuardRefusesWhileAPendingRollbackIsStillPreReboot)
{
    install_firmware();
    reboot();
    rollback_firmware();
    ASSERT_EQ(env("update_reboot_state"), "7");
    ASSERT_FALSE(bootstate->pendingFirmwareUpdate());
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware(), fs::GenericException);

    EXPECT_EQ(snapshot(), before);
}

TEST_F(BootstateTest, FirmwareRollbackAfterRebootMarksTheSlotBadOnCommit)
{
    install_firmware();
    reboot();
    rollback_firmware();

    EXPECT_EQ(env("BOOT_A_LEFT"), "0");
    EXPECT_EQ(env("update_reboot_state"), "7");

    reboot();
    ASSERT_EQ(running_slot(), "B");
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

// A switch between two already-committed slots (switch_firmware_slot with
// nothing pending), reflected in firmware_reboot() before and after the
// reboot. --apply_update's own rollback marker lives in the tmpfs work
// directory and does not survive a reboot the caller ran directly instead of
// through --apply_update; firmware_reboot()'s update_reboot_state==7 branch
// is what a marker-free apply now falls back on.
//
// BOOT_B_LEFT starts drained (an earlier, unrelated boot already spent an
// attempt on it) instead of at the fixture's default 3: a counter-based
// "was there ever a reboot" check would misread that as one already having
// happened, so this is what actually exercises the update_reboot_state==7
// branch instead of passing on the drained-counter coincidence.
TEST_F(BootstateTest, FirmwareRebootReflectsASwitchBetweenCommittedSlots)
{
    set("rauc_cmd", "rauc.slot=A");
    set("BOOT_ORDER", "A B");
    set("BOOT_ORDER_OLD", "A B");
    set("BOOT_B_LEFT", "2");
    set("update", "2000"); // fw_a abandoned (bad), fw_b committed and clean

    rollback_firmware();
    ASSERT_EQ(env("update_reboot_state"), "7");
    ASSERT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_FALSE(bootstate->firmware_reboot());

    reboot();
    ASSERT_EQ(running_slot(), "B");
    EXPECT_EQ(env("update_reboot_state"), "7");
    EXPECT_TRUE(bootstate->firmware_reboot());
}

// rollback_firmware()'s own urs=7 write (handleUpdate.cpp, the pending-update
// branch) never touches BOOT_ORDER, only BOOT_<current>_LEFT, so the running
// slot still leads BOOT_ORDER right after --rollback_update. That slot still
// carries its install digit, which is what tells this apart from a switch
// between committed slots: firmware_reboot() must read "not rebooted" until
// the bootloader has skipped the drained slot.
TEST_F(BootstateTest, FirmwareRebootReadsFalseRightAfterRollbackBeforeAnyReboot)
{
    install_firmware();
    reboot();
    ASSERT_TRUE(bootstate->pendingFirmwareUpdate());

    rollback_firmware();
    ASSERT_EQ(env("update_reboot_state"), "7");

    EXPECT_FALSE(bootstate->firmware_reboot());

    reboot();
    ASSERT_EQ(running_slot(), "B");
    ASSERT_EQ(env("BOOT_ORDER"), "A B");
    EXPECT_TRUE(bootstate->firmware_reboot());
}

// A stray digit on the slot a switch leaves must not read as the rollback
// of an unconfirmed install: only the digit of the slot leading BOOT_ORDER
// counts.
TEST_F(BootstateTest, FirmwareRebootIgnoresAStrayDigitOnTheSlotASwitchLeaves)
{
    set("rauc_cmd", "rauc.slot=A");
    set("BOOT_ORDER", "A B");
    set("BOOT_ORDER_OLD", "A B");
    set("update", "1000");

    rollback_firmware();
    ASSERT_EQ(env("update_reboot_state"), "7");
    ASSERT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_FALSE(bootstate->firmware_reboot());

    reboot();
    ASSERT_EQ(running_slot(), "B");
    EXPECT_TRUE(bootstate->firmware_reboot());
}

// A switch onto a slot with a single attempt left drains it to 0 on the
// reboot; that must not look like the drained slot of a rolled-back install.
TEST_F(BootstateTest, FirmwareRebootReadsTrueAfterASwitchOntoALastAttemptSlot)
{
    set("rauc_cmd", "rauc.slot=A");
    set("BOOT_ORDER", "A B");
    set("BOOT_ORDER_OLD", "A B");
    set("BOOT_B_LEFT", "1");
    set("update", "2000");

    rollback_firmware();
    ASSERT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_FALSE(bootstate->firmware_reboot());

    reboot();
    ASSERT_EQ(running_slot(), "B");
    ASSERT_EQ(env("BOOT_B_LEFT"), "0");
    EXPECT_TRUE(bootstate->firmware_reboot());
}

// A BOOT_ORDER with a single slot (RAUC mid-write) never counts as rebooted.
TEST_F(BootstateTest, FirmwareRebootReadsFalseForASingleSlotBootOrder)
{
    set("rauc_cmd", "rauc.slot=B");
    set("BOOT_ORDER", "B");
    set("update_reboot_state", "7");

    EXPECT_FALSE(bootstate->firmware_reboot());
}

// The combined rollback (urs 9) leaves the drained slot in front with its
// install digit set, like the firmware-only one.
TEST_F(BootstateTest, FirmwareRebootReadsFalseForACombinedRollbackUntilItsReboot)
{
    install_firmware_and_application();
    reboot();
    ASSERT_TRUE(bootstate->pendingApplicationFirmwareUpdate());

    rollback_firmware_and_application();
    ASSERT_EQ(env("update_reboot_state"), "9");
    EXPECT_FALSE(bootstate->firmware_reboot());

    reboot();
    ASSERT_EQ(running_slot(), "B");
    EXPECT_TRUE(bootstate->firmware_reboot());
}

// A switch onto a slot without attempts left boots back into the slot it
// left, so the reboot never reads as done: a known limit, an apply asks for
// another reboot instead of settling.
TEST_F(BootstateTest, FirmwareRebootStaysFalseWhenTheSwitchTargetCannotBoot)
{
    set("rauc_cmd", "rauc.slot=A");
    set("BOOT_ORDER", "A B");
    set("BOOT_ORDER_OLD", "A B");
    set("BOOT_B_LEFT", "0");
    set("update", "2000");

    rollback_firmware();
    ASSERT_EQ(env("BOOT_ORDER"), "B A");

    reboot();
    ASSERT_EQ(running_slot(), "A");
    EXPECT_FALSE(bootstate->firmware_reboot());
}

// The application rollback (urs 8) reads the mounted image, which is the old
// one until the reboot.
TEST_F(BootstateTest, ApplicationRebootReadsFalseAfterARollbackUntilItsReboot)
{
    install_application();
    reboot();
    rollback_application();
    ASSERT_EQ(env("update_reboot_state"), "8");
    EXPECT_FALSE(bootstate->application_reboot());

    reboot();
    EXPECT_TRUE(bootstate->application_reboot());
}

// commit_update()'s fallback branch decides on pendingUpdateRollback(), which
// for urs==7 defers to pendingFirmwareRollback() -- a second, independent
// reader of "did the reboot happen" from firmware_reboot()'s. A switch
// between committed slots followed by a reboot bypassing --apply_update
// must let a direct --commit_update through exactly like
// firmware_reboot() does.
TEST_F(BootstateTest, PendingFirmwareRollbackHoldsAfterASwitchBetweenCommittedSlotsReboots)
{
    set("rauc_cmd", "rauc.slot=A");
    set("BOOT_ORDER", "A B");
    set("BOOT_ORDER_OLD", "A B");
    set("BOOT_B_LEFT", "2");
    set("update", "0000"); // both slots committed and clean, as measured on the board

    rollback_firmware();
    ASSERT_EQ(env("update_reboot_state"), "7");

    reboot();
    ASSERT_EQ(running_slot(), "B");
    ASSERT_EQ(env("update_reboot_state"), "7");

    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update_reboot_state"), "0");
    EXPECT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_EQ(env("BOOT_ORDER_OLD"), "B A");
    EXPECT_EQ(env("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env("BOOT_B_LEFT"), "3");
}

// pendingUpdateRollback()'s urs==7/9 branches call pendingFirmwareRollback()
// unconditionally now (the digit-presence shortcut that
// used to bypass it is gone); this pins that the combined case goes through it too,
// refusing a commit whose forced reboot has not happened yet, the same as
// FirmwareCommitBeforeTheRollbacksOwnRebootIsRefused pins for the fw-only
// case above.
TEST_F(BootstateTest, PendingFirmwareRollbackRefusesCombinedRollbackBeforeItsForcedReboot)
{
    install_firmware_and_application();
    reboot();
    ASSERT_TRUE(bootstate->pendingApplicationFirmwareUpdate());

    rollback_firmware_and_application();
    ASSERT_EQ(env("update_reboot_state"), "9");

    const auto before = snapshot();
    EXPECT_THROW(commit(), fs::NotAllowedUpdateState);
    EXPECT_EQ(snapshot(), before);
}

// --- firmware rolled back before the reboot ---------------------------------

// The written slot is neither cleared nor marked: the rollback after the
// reboot is the only one that touches it. A second attempt is refused the
// same way.
TEST_F(BootstateTest, FirmwareRollbackWithoutRebootIsRefused)
{
    install_firmware();
    ASSERT_TRUE(installed());
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);
    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
    EXPECT_EQ(env("update"), "1000");
    EXPECT_EQ(env("update_reboot_state"), "2");
    EXPECT_TRUE(installed());
    EXPECT_TRUE(bootstate->pendingFirmwareUpdate());
    EXPECT_FALSE(bootstate->firmware_reboot());
}

// A single-slot BOOT_ORDER (RAUC still owns the other slot mid-write) makes
// firmware_update_reboot_successful/_failed/missing_firmware_update_reboot
// all read false by design (their own size()==2 guard), so neither a
// completed nor a missing reboot can be told from a write in progress.
// commit_update()'s dispatch has nothing left to fall back on and throws
// FirmwareRebootStateNotDefined -- the CLI now answers that the same way a
// plain state query would instead of a raw internal error.
TEST_F(BootstateTest, CommitDuringASingleSlotBootOrderIsUndecidable)
{
    install_firmware();
    reboot();
    ASSERT_TRUE(bootstate->pendingFirmwareUpdate());

    set("BOOT_ORDER", running_slot());

    EXPECT_THROW(commit(), updater::FirmwareRebootStateNotDefined);
}

TEST_F(BootstateTest, FirmwareRollbackWithoutRebootIsRefusedFromSlotA)
{
    set("rauc_cmd", "rauc.slot=A");
    set("BOOT_ORDER", "A B");
    set("BOOT_ORDER_OLD", "A B");
    set("update", "0000");
    install_firmware();
    ASSERT_EQ(env("update"), "0010");
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
    EXPECT_TRUE(bootstate->pendingFirmwareUpdate());
}

// An application cycle leaves the running slot with a spent attempt, which
// the boot-order predicates read as no known state.
TEST_F(BootstateTest, FirmwareRollbackWithoutRebootIsRefusedWithASpentAttempt)
{
    set("BOOT_B_LEFT", "2");
    install_firmware();
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
    EXPECT_EQ(env("BOOT_B_LEFT"), "2");
    EXPECT_TRUE(bootstate->pendingFirmwareUpdate());
}

// An application update that was never committed leaves its digit on the slot
// the firmware install then writes. The pending predicates read that slot as
// a combined update and miss it, so the rollback reaches the switch between
// committed slots, whose digit check answers "commit required" for the slot
// the install just wrote. The marker has to decide before it.
TEST_F(BootstateTest, FirmwareRollbackWithoutRebootIsRefusedWithAnUncommittedApplicationDigit)
{
    set("update", "0100");
    install_firmware();
    ASSERT_EQ(env("update"), "1100");
    ASSERT_EQ(env("update_reboot_state"), "2");
    ASSERT_FALSE(bootstate->pendingFirmwareUpdate());
    ASSERT_FALSE(bootstate->pendingApplicationFirmwareUpdate());
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);
    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
    EXPECT_TRUE(installed());
}

// After the reboot the application digit sits on the slot that is running, so
// the update is pending again and the rollback runs as it always did.
TEST_F(BootstateTest, FirmwareRollbackWithAnUncommittedApplicationDigitWorksAfterTheReboot)
{
    set("update", "0100");
    install_firmware();
    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);

    reboot();
    ASSERT_EQ(running_slot(), "A");
    ASSERT_FALSE(installed());
    ASSERT_TRUE(bootstate->pendingFirmwareUpdate());

    rollback_firmware();

    EXPECT_EQ(env("BOOT_A_LEFT"), "0");
    EXPECT_EQ(env("update_reboot_state"), "7");
    EXPECT_EQ(env("update"), "1100");
}

// Without the marker and without a reboot the boot-order predicates alone
// still refuse.
TEST_F(BootstateTest, FirmwareRollbackWithoutMarkerBeforeRebootIsStillRefused)
{
    install_firmware();
    std::filesystem::remove(marker);
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
}

// The refusal leaves the update pending, so the rollback after the reboot
// still marks the slot as it always did.
TEST_F(BootstateTest, FirmwareRollbackAfterRefusalAndRebootStillWorks)
{
    install_firmware();
    EXPECT_THROW(rollback_firmware(), updater::MissingReboot);

    reboot();
    ASSERT_FALSE(installed());
    ASSERT_EQ(running_slot(), "A");
    rollback_firmware();
    EXPECT_EQ(env("BOOT_A_LEFT"), "0");
    EXPECT_EQ(env("update_reboot_state"), "7");

    reboot();
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("BOOT_ORDER"), "B A");
}

// Full counters after the reboot (a mark-good ran) do not read as a missing
// reboot once the marker is gone.
TEST_F(BootstateTest, FirmwareRollbackAfterRebootWithFullCountersIsNotRefused)
{
    install_firmware();
    reboot();
    set("BOOT_A_LEFT", "3");
    set("BOOT_B_LEFT", "3");
    ASSERT_FALSE(installed());

    rollback_firmware();

    EXPECT_EQ(env("BOOT_A_LEFT"), "0");
    EXPECT_EQ(env("update_reboot_state"), "7");
}

// --- combined update, rolled back before the reboot -------------------------

TEST_F(BootstateTest, CombinedRollbackWithoutRebootIsRefused)
{
    install_firmware_and_application();
    ASSERT_EQ(env("update"), "1100");
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware_and_application(), updater::MissingReboot);
    EXPECT_THROW(rollback_firmware_and_application(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
    EXPECT_EQ(env("update_reboot_state"), "4");
    EXPECT_TRUE(installed());
    EXPECT_TRUE(bootstate->pendingApplicationFirmwareUpdate());
}

TEST_F(BootstateTest, CombinedRollbackWithoutRebootIsRefusedWithASpentAttempt)
{
    set("BOOT_B_LEFT", "2");
    install_firmware_and_application();
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware_and_application(), updater::MissingReboot);

    EXPECT_EQ(snapshot(), before);
}

TEST_F(BootstateTest, CombinedRollbackAfterRefusalAndRebootStillWorks)
{
    install_firmware_and_application();
    EXPECT_THROW(rollback_firmware_and_application(), updater::MissingReboot);

    reboot();
    ASSERT_FALSE(installed());
    rollback_firmware_and_application();
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update_reboot_state"), "9");

    reboot();
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2200");
}

// --- combined update, rolled back after the reboot ----------------------------

TEST_F(BootstateTest, CombinedRollbackAfterRebootMarksBothSlotsBadOnCommit)
{
    install_firmware_and_application();
    EXPECT_EQ(env("update"), "1100");
    reboot();
    rollback_firmware_and_application();

    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update"), "1100");
    EXPECT_EQ(env("update_reboot_state"), "9");

    reboot();
    ASSERT_EQ(running_slot(), "B");
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2200");
    EXPECT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

// --- combined install stopped before it named the application slot ------------

// "application" still names the running slot, the new firmware runs and the
// application digit sits on the other slot. No command may flip to that slot;
// the commit clears both digits.
TEST_F(BootstateTest, InterruptedCombinedInstallIsPendingAfterReboot)
{
    interrupt_firmware_and_application_install();
    reboot();

    ASSERT_EQ(running_slot(), "A");
    EXPECT_EQ(env("update"), "1100");
    EXPECT_EQ(env("application"), "B");
    EXPECT_TRUE(bootstate->pendingApplicationFirmwareUpdate());
    EXPECT_TRUE(bootstate->app_install_unnamed());
    EXPECT_FALSE(bootstate->pendingApplicationUpdate());
}

TEST_F(BootstateTest, CompletedCombinedInstallIsNotUnnamed)
{
    install_firmware_and_application();
    reboot();

    EXPECT_TRUE(bootstate->pendingApplicationFirmwareUpdate());
    EXPECT_FALSE(bootstate->app_install_unnamed());
}

TEST_F(BootstateTest, InterruptedCombinedInstallCommitClearsBothDigitsWithoutFlipping)
{
    interrupt_firmware_and_application_install();
    reboot();

    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "0000");
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("BOOT_ORDER_OLD"), "A B");
    EXPECT_EQ(env("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env("BOOT_B_LEFT"), "3");
    EXPECT_EQ(env("update_reboot_state"), "0");
    EXPECT_FALSE(commit());
}

TEST_F(BootstateTest, InterruptedCombinedInstallCommitBeforeRebootIsRefused)
{
    interrupt_firmware_and_application_install();
    const auto before = snapshot();

    EXPECT_THROW(commit(), updater::MissingReboot);
    EXPECT_EQ(snapshot(), before);
}

TEST_F(BootstateTest, InterruptedCombinedInstallRollbackIsRefused)
{
    interrupt_firmware_and_application_install();
    reboot();
    const auto before = snapshot();

    for (int i = 0; i < 2; i++) {
        {
            UBoot::UBoot::EnvTransaction txn(*uboot);
            EXPECT_THROW(bootstate->applicaton_rollback(*app_updater), updater::CommitRequired);
        }
        {
            UBoot::UBoot::EnvTransaction txn(*uboot);
            EXPECT_THROW(bootstate->firmware_rollback(), updater::CommitRequired);
        }
    }

    EXPECT_EQ(snapshot(), before);
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "0000");
    EXPECT_EQ(env("application"), "B");
}

// The command line asks the application half first. Without a marker it used
// to answer "commit required", and the commit then answered "reboot".
TEST_F(BootstateTest, InterruptedCombinedInstallRollbackBeforeRebootAsksForTheReboot)
{
    interrupt_firmware_and_application_install();
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware_and_application(), updater::MissingReboot);
    EXPECT_EQ(snapshot(), before);
    EXPECT_THROW(commit(), updater::MissingReboot);
    EXPECT_EQ(snapshot(), before);
}

TEST_F(BootstateTest, InterruptedCombinedInstallGuardStaysQuietAfterTheReboot)
{
    interrupt_firmware_and_application_install();
    reboot();
    const auto before = snapshot();

    EXPECT_NO_THROW(bootstate->refuse_rollback_before_reboot());
    EXPECT_THROW(rollback_firmware_and_application(), updater::CommitRequired);
    EXPECT_EQ(snapshot(), before);
    EXPECT_TRUE(commit());
}

TEST_F(BootstateTest, CombinedRollbackWithoutMarkerBeforeRebootIsRefused)
{
    install_firmware_and_application();
    std::filesystem::remove(marker);
    const auto before = snapshot();

    EXPECT_THROW(rollback_firmware_and_application(), updater::MissingReboot);
    EXPECT_EQ(snapshot(), before);
}

// The guard decides before the loop device is read.
TEST_F(BootstateTest, InterruptedCombinedInstallRollbackBeforeRebootNeedsNoLoopDevice)
{
    interrupt_firmware_and_application_install();
    unmount();

    EXPECT_THROW(rollback_firmware_and_application(), updater::MissingReboot);
}

// Known limit, pins what is: a spent attempt on the running slot reads as a
// reboot that already happened, so only a real reboot gets out of this state.
TEST_F(BootstateTest, InterruptedCombinedInstallWithASpentAttemptIsAKnownLimit)
{
    interrupt_firmware_and_application_install();
    set("BOOT_A_LEFT", "2");
    const auto before = snapshot();

    EXPECT_NO_THROW(bootstate->refuse_rollback_before_reboot());
    EXPECT_THROW(rollback_firmware_and_application(), updater::CommitRequired);
    EXPECT_THROW(commit(), updater::FirmwareRebootStateNotDefined);
    EXPECT_EQ(snapshot(), before);
}

// The new firmware did not boot: the old slot runs and "application" was never
// switched, so the failed-update commit must not switch it either.
TEST_F(BootstateTest, InterruptedCombinedInstallCommitAfterFailedRebootKeepsTheApplication)
{
    interrupt_firmware_and_application_install();
    set("BOOT_A_LEFT", "0");
    reboot();
    ASSERT_EQ(running_slot(), "B");
    ASSERT_TRUE(bootstate->pendingApplicationFirmwareUpdate());
    ASSERT_TRUE(bootstate->app_install_unnamed());

    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2000");
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_EQ(env("BOOT_A_LEFT"), "3");
    EXPECT_EQ(env("update_reboot_state"), "0");
}


} // namespace
