#include <gtest/gtest.h>

#include "handle_update/app_bundle_install.h"
#include "handle_update/updateApplication.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "uboot_interface/IUBootEnv.h"
#include "uboot_interface/uboot_exceptions.h"
#include "support/fake_uboot_env.h"

#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using test_support::FakeUBootEnv;

std::filesystem::path make_temp_dir(const char *tag)
{
    std::filesystem::path p =
        std::filesystem::temp_directory_path() / (std::string("fs-updater-app-update-") + tag + "-" + std::to_string(::getpid()));
    std::filesystem::create_directories(p);
    return p;
}

void write_file(const std::filesystem::path &p, const std::string &content)
{
    std::ofstream f(p, std::ios::binary);
    f << content;
}

std::shared_ptr<logger::LoggerHandler> test_logger()
{
    static auto handler = logger::LoggerHandler::initLogger(std::make_shared<logger::LoggerSinkEmpty>(logger::logLevel::ERROR));
    return handler;
}

/* CertificateVerifier only opens the keyring lazily, on first
 * verify_certificate_chain() call. Every test here exercises the RAUC-bundle
 * branch of install(), which never touches CertificateVerifier at all ("RAUC
 * verifies the bundle signature ... no F&S image header or embedded
 * certificate layer to check") — so the keyring never needs to exist, only
 * this INI file naming it. */
std::filesystem::path make_rauc_config(const std::filesystem::path &dir)
{
    auto path = dir / "system.conf";
    write_file(path, "[keyring]\npath = keyring.pem\n");
    return path;
}

std::string rauc_bundle_bytes()
{
    std::string bytes(64, '\0');
    std::memcpy(&bytes[0], "hsqs", 4);
    return bytes;
}

/* Seams out the real RAUC D-Bus install (applicationUpdate::install_bundle_via_rauc)
 * so these tests never need a running RAUC service. stage_incoming_on_install
 * simulates what the real bundle's install hook does in production: write the
 * payload + its 3 verity sidecars to <images_dir>/.incoming.squashfs{,.verity,
 * .roothash,.roothash.p7s} before returning — activate_incoming_app_image()
 * requires the full 4-file set. */
class FakeApplicationUpdate : public updater::applicationUpdate
{
  public:
    FakeApplicationUpdate(const std::shared_ptr<UBoot::IUBootEnv> &uboot_ptr, const std::shared_ptr<logger::LoggerHandler> &logger,
                          const std::string &rauc_config_path, const std::string &app_image_store_path)
        : updater::applicationUpdate(uboot_ptr, logger, rauc_config_path, app_image_store_path),
          images_dir_(app_image_store_path)
    {
    }

    bool fail_rauc_install = false;
    bool stage_incoming_on_install = true;
    std::vector<std::string> rauc_install_calls;

  protected:
    void install_bundle_via_rauc(const std::string &path_to_bundle) override
    {
        rauc_install_calls.push_back(path_to_bundle);
        if (fail_rauc_install)
        {
            throw std::runtime_error("fake RAUC install failure");
        }
        if (stage_incoming_on_install)
        {
            write_file(std::filesystem::path(images_dir_) / ".incoming.squashfs", "fake-app-payload");
            for (const char *suffix : fs::kAppImageSidecarSuffixes)
            {
                write_file(std::filesystem::path(images_dir_) / (".incoming.squashfs" + std::string(suffix)),
                          std::string("fake-sidecar") + suffix);
            }
        }
    }

  private:
    std::string images_dir_;
};

struct ApplicationUpdateFixture : public ::testing::Test
{
    std::filesystem::path images_dir;
    std::filesystem::path config_path;
    std::filesystem::path bundle_path;
    std::shared_ptr<FakeUBootEnv> env;

    void SetUp() override
    {
        images_dir = make_temp_dir("fixture");
        config_path = make_rauc_config(images_dir);
        bundle_path = images_dir / "app.raucb";
        write_file(bundle_path, rauc_bundle_bytes());
        /* The reboot-state variable is seeded with content no reader can
         * interpret: the install door must neither act on it nor repair it. */
        env = std::make_shared<FakeUBootEnv>(
            std::map<std::string, std::string>{{"application", "A"}, {"update_reboot_state", "13"}});
    }

    void TearDown() override { std::filesystem::remove_all(images_dir); }

    std::unique_ptr<FakeApplicationUpdate> make_update()
    {
        /* Trailing '/' matches production's STANDARD_APP_IMG_STORE convention
         * (perform_installation/install_rauc_bundle both string-concat onto it). */
        return std::make_unique<FakeApplicationUpdate>(env, test_logger(), config_path.string(), images_dir.string() + "/");
    }
};

TEST_F(ApplicationUpdateFixture, RaucInstallFlipsApplicationVarAndActivatesImage)
{
    auto update = make_update();

    update->install(bundle_path.string());

    EXPECT_EQ(update->rauc_install_calls.size(), 1U);
    EXPECT_EQ(update->rauc_install_calls.front(), bundle_path.string());
    EXPECT_EQ(env->at("application"), "B");
    EXPECT_TRUE(std::filesystem::exists(images_dir / "app_b.squashfs"));
    EXPECT_FALSE(std::filesystem::exists(images_dir / ".incoming.squashfs"));
}

TEST_F(ApplicationUpdateFixture, RaucInstallFailureLeavesApplicationVarUnchanged)
{
    auto update = make_update();
    update->fail_rauc_install = true;

    EXPECT_THROW(update->install(bundle_path.string()), std::exception);

    EXPECT_EQ(env->at("application"), "A");
    EXPECT_FALSE(std::filesystem::exists(images_dir / "app_b.squashfs"));
}

TEST_F(ApplicationUpdateFixture, ActivateFailureAfterRaucSuccessLeavesApplicationVarUnchanged)
{
    auto update = make_update();
    /* RAUC "succeeds" but its install hook never staged .incoming.squashfs
     * (e.g. the hook itself failed silently, or wrote elsewhere) — activation
     * must fail loudly rather than flip the boot variable onto a slot that
     * was never actually written. */
    update->stage_incoming_on_install = false;

    EXPECT_THROW(update->install(bundle_path.string()), std::exception);

    EXPECT_EQ(update->rauc_install_calls.size(), 1U);
    EXPECT_EQ(env->at("application"), "A");
}

/* The install door writes the application slot and nothing else on this
 * variable: an installer that "corrected" an unreadable durable state would
 * persist a value it invented, and a fallback onto an older image would then
 * read it back. */
TEST_F(ApplicationUpdateFixture, InstallLeavesAnUninterpretableRebootStateUntouched)
{
    auto update = make_update();

    update->install(bundle_path.string());

    EXPECT_EQ(env->at("application"), "B");
    EXPECT_EQ(env->at("update_reboot_state"), "13");
    EXPECT_TRUE(env->writes_of("update_reboot_state").empty());
}

TEST_F(ApplicationUpdateFixture, RollbackFlipsApplicationVarBack)
{
    auto update = make_update();
    update->install(bundle_path.string());
    ASSERT_EQ(env->at("application"), "B");

    update->rollback();
    /* Unlike install(), rollback() doesn't flush itself in production -
     * flushing pending-rollback state is the caller's (Bootstate's) job,
     * timed against the reboot-state transition. */
    env->flushEnvironment();

    EXPECT_EQ(env->at("application"), "A");
}

} // namespace
