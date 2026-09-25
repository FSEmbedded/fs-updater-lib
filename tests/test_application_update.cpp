#include <gtest/gtest.h>

#include "handle_update/app_bundle_install.h"
#include "handle_update/updateApplication.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "uboot_interface/IUBootEnv.h"
#include "uboot_interface/uboot_exceptions.h"
#include "support/fake_uboot_env.h"

#include <climits>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
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

/* Keeps every log line so a test can assert a fact the object graph does not
 * otherwise expose. initLogger() keys its handler on the sink, so a private
 * sink yields a private handler and these lines never mix with another test's.
 *
 * The handler drains its queue on a worker thread, so entries arrive AFTER the
 * call that logged them returned: the mutex is not decoration, and a reader
 * has to wait for delivery instead of assuming it. */
class CapturingSink : public logger::LoggerSinkBase
{
  public:
    void setLogEntry(const std::shared_ptr<logger::LogEntry> &ptr) override
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(ptr->getLogMessage());
    }

    /* Bounded on purpose: a line that never arrives must fail the test, not
     * hang the suite. */
    bool wait_for(const std::string &needle, std::chrono::milliseconds budget)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        do
        {
            for (const std::string &line : snapshot())
            {
                if (line.find(needle) != std::string::npos)
                {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    std::vector<std::string> snapshot()
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        return lines_;
    }

  private:
    std::mutex mutex_;
    std::vector<std::string> lines_;
};

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
                          const std::string &rauc_config_path, const std::string &app_image_store_path,
                          const std::string &app_version_file = updater::config::PATH_TO_APPLICATION_VERSION_FILE)
        : updater::applicationUpdate(uboot_ptr, logger, rauc_config_path, app_image_store_path, app_version_file),
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

/* A failed environment write must not leave the new slot pointer staged. The
 * caller's error handler stages its failure state on the same object and
 * flushes; anything left behind rides along and flips the slot the install
 * never completed, while the state written says the update failed. */
TEST_F(ApplicationUpdateFixture, FailedEnvironmentWriteLeavesNoStagedSlotPointer)
{
    auto update = make_update();
    ASSERT_EQ(env->at("application"), "A");

    env->fail_next_flush();
    EXPECT_THROW(update->install(bundle_path.string()), std::exception);

    /* The attempt is on record -- it happened -- but nothing of it may survive
     * into the next write. */
    EXPECT_FALSE(env->writes_of("application").empty());

    /* Stand in for the caller's error handler: stage the failure state and
     * flush on the same object. */
    env->addVariable("update_reboot_state", "6");
    env->flushEnvironment();

    EXPECT_EQ(env->at("application"), "A") << "the failed install flipped the slot anyway";
    EXPECT_EQ(env->at("update_reboot_state"), "6");
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

/* The config that won the search and the keyring it names must stay one pair.
 * The derivation itself is covered as a pure function in
 * test_rauc_config_path.cpp; what is pinned HERE is that the constructor feeds
 * it the path it actually loaded. Passing the compiled-in default instead is a
 * one-token regression that every other test in this file survives, and on a
 * device it would send the verifier to a trust root the image no longer
 * ships. */
TEST_F(ApplicationUpdateFixture, KeyringIsResolvedBesideTheConfigThatWasLoaded)
{
    auto sink = std::make_shared<CapturingSink>();
    auto capturing_logger = logger::LoggerHandler::initLogger(sink);

    FakeApplicationUpdate update(env, capturing_logger, config_path.string(),
                                 images_dir.string() + "/");

    const std::string wanted = "keyring " + images_dir.string() + "/keyring.pem";
    EXPECT_TRUE(sink->wait_for(wanted, std::chrono::seconds(5)))
        << "no log line named the keyring resolved beside " << config_path.string();

    /* The failure this guards against, stated positively: never the
     * compiled-in directory when the config came from somewhere else. */
    for (const std::string &line : sink->snapshot())
    {
        EXPECT_EQ(line.find("keyring /etc/rauc/"), std::string::npos) << line;
    }
}

#if UPDATE_VERSION_TYPE_STRING == 1
/* The version is read from the file the constructor was given, not from a
 * fixed /etc path: a BSP that ships the file inside the application mount
 * points the library there. */
struct ApplicationVersionFixture : public ApplicationUpdateFixture
{
    std::filesystem::path version_file;

    void SetUp() override
    {
        ApplicationUpdateFixture::SetUp();
        version_file = images_dir / "opt-app" / "etc" / "app_version";
        std::filesystem::create_directories(version_file.parent_path());
    }

    FakeApplicationUpdate make_reader()
    {
        return FakeApplicationUpdate(env, test_logger(), config_path.string(), images_dir.string() + "/",
                                     version_file.string());
    }
};

TEST_F(ApplicationVersionFixture, ReadsTheFirstLineOfTheConfiguredFile)
{
    write_file(version_file, "20260925\nignored\n");
    EXPECT_EQ(make_reader().getCurrentVersion(), "20260925");
}

TEST_F(ApplicationVersionFixture, ReadsAVersionWithoutATrailingNewline)
{
    write_file(version_file, "1.0.0");
    EXPECT_EQ(make_reader().getCurrentVersion(), "1.0.0");
}

/* Pins today's behaviour: an empty file is "no version", not an error. The
 * service relies on an empty answer meaning exactly that. */
TEST_F(ApplicationVersionFixture, AnEmptyFileYieldsAnEmptyVersion)
{
    write_file(version_file, "");
    EXPECT_EQ(make_reader().getCurrentVersion(), "");
}

/* The path is per-BSP, so the error has to say which file it tried. */
TEST_F(ApplicationVersionFixture, AMissingFileThrowsNamingThePath)
{
    try
    {
        (void)make_reader().getCurrentVersion();
        FAIL() << "no exception for a missing version file";
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_NE(std::string(e.what()).find(version_file.string()), std::string::npos) << e.what();
    }
}

TEST_F(ApplicationVersionFixture, AnUnreadableFileThrows)
{
    if (::geteuid() == 0)
    {
        GTEST_SKIP() << "root reads a mode-0000 file";
    }
    write_file(version_file, "20260925\n");
    std::filesystem::permissions(version_file, std::filesystem::perms::none);
    EXPECT_THROW(make_reader().getCurrentVersion(), std::runtime_error);
    std::filesystem::permissions(version_file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
#endif
