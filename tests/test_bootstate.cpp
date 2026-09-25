#include <gtest/gtest.h>

#include "fake_env.h"
#include "handle_update/fs_exceptions.h"
#include "handle_update/handleUpdate.h"
#include "handle_update/updateBase.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "uboot_interface/UBoot.h"
#include "uboot_interface/allowed_uboot_variable_states.h"

#include <filesystem>
#include <fstream>
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

    void TearDown() override { std::filesystem::remove(backing_file); }

    std::string env(const std::string &name) const { return fake_env::flash().at(name); }
    void set(const std::string &name, const std::string &value) { fake_env::flash()[name] = value; }

    // The loop device's backing file names the mounted application image.
    void mount(const std::string &image) { std::ofstream(backing_file) << image << '\n'; }
    void unmount() { std::filesystem::remove(backing_file); }

    std::string running_slot() const { return env("rauc_cmd").substr(env("rauc_cmd").find('=') + 1); }
    static std::string other(const std::string &slot) { return slot == "A" ? "B" : "A"; }

    // Boot the first slot of BOOT_ORDER that still has attempts left and mount
    // the application slot named by "application".
    void reboot()
    {
        std::string slot = env("BOOT_ORDER").substr(0, 1);
        if (env("BOOT_" + slot + "_LEFT") == "0") {
            slot = other(slot);
        }
        set("rauc_cmd", "rauc.slot=" + slot);
        mount(env("application") == "A" ? APP_A_IMAGE : APP_B_IMAGE);
    }

    // FSUpdate::update_application: the pre-write before applicationUpdate::install(),
    // then what a completed install() flushes (the target slot in "application").
    void install_application()
    {
        {
            UBoot::UBoot::EnvTransaction txn(*uboot);
            std::string update = env("update");
            update.at(bootstate->get_update_bit(Flags::APP, true)) = '1';
            uboot->addVariable("update", update);
            uboot->addVariable("update_reboot_state",
                               update_definitions::to_string(UBootBootstateFlags::INCOMPLETE_APP_UPDATE));
            uboot->flushEnvironment();
        }
        uboot->addVariable("application", other(env("application")));
        uboot->flushEnvironment();
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
    }

    // FSUpdate::update_firmware_and_application: firmware first, then the
    // application pre-write on top of it and the combined state.
    void install_firmware_and_application()
    {
        install_firmware(UBootBootstateFlags::INCOMPLETE_FW_UPDATE);
        {
            UBoot::UBoot::EnvTransaction txn(*uboot);
            std::string update = env("update");
            update.at(bootstate->get_update_bit(Flags::APP, true)) = '1';
            uboot->addVariable("update", update);
            uboot->addVariable("update_reboot_state",
                               update_definitions::to_string(UBootBootstateFlags::INCOMPLETE_APP_FW_UPDATE));
            uboot->flushEnvironment();
        }
        uboot->addVariable("application", other(env("application")));
        uboot->flushEnvironment();
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
        ASSERT_TRUE(bootstate->pendingApplicationUpdate());
        bootstate->applicaton_rollback(*app_updater);
        uboot->flushEnvironment();
    }

    // FSUpdate::rollback_firmware for a pending firmware-only update.
    void rollback_firmware()
    {
        UBoot::UBoot::EnvTransaction txn(*uboot);
        ASSERT_TRUE(bootstate->pendingFirmwareUpdate());
        bootstate->firmware_rollback();
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

// --- combined update, rolled back after the reboot ----------------------------

TEST_F(BootstateTest, CombinedRollbackAfterRebootMarksBothSlotsBadOnCommit)
{
    install_firmware_and_application();
    EXPECT_EQ(env("update"), "1100");
    reboot();
    ASSERT_TRUE(bootstate->pendingApplicationFirmwareUpdate());

    // FSUpdate::rollback_firmware for a pending combined update.
    {
        UBoot::UBoot::EnvTransaction txn(*uboot);
        bootstate->firmware_rollback();
        app_updater->rollback();
        uboot->addVariable("update_reboot_state",
                           update_definitions::to_string(UBootBootstateFlags::ROLLBACK_APP_FW_REBOOT_PENDING));
        uboot->flushEnvironment();
    }
    EXPECT_EQ(env("application"), "B");
    EXPECT_EQ(env("update_reboot_state"), "9");

    reboot();
    ASSERT_EQ(running_slot(), "B");
    EXPECT_TRUE(commit());
    EXPECT_EQ(env("update"), "2200");
    EXPECT_EQ(env("BOOT_ORDER"), "B A");
    EXPECT_EQ(env("update_reboot_state"), "0");
}

} // namespace
