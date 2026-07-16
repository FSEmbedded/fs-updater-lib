#include <gtest/gtest.h>

#include "handle_update/app_bundle_install.h"
#include "handle_update/fs_exceptions.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

std::filesystem::path make_temp_dir(const char* tag)
{
    std::filesystem::path p = std::filesystem::temp_directory_path() /
        (std::string("fs-updater-app-bundle-") + tag + "-" + std::to_string(::getpid()));
    std::filesystem::create_directories(p);
    return p;
}

void write_file(const std::filesystem::path& p, const std::string& content)
{
    std::ofstream f(p, std::ios::binary);
    f << content;
}

std::string read_file(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Stage a complete 4-file incoming set (the squashfs + its 3 verity
// sidecars), each with distinguishable content so a test can verify every
// file actually moved, not just the squashfs.
void write_incoming_set(const std::filesystem::path& dir, const std::string& squashfs_content)
{
    write_file(dir / ".incoming.squashfs", squashfs_content);
    for (const char* suffix : fs::kAppImageSidecarSuffixes) {
        write_file(dir / (".incoming.squashfs" + std::string(suffix)), squashfs_content + suffix);
    }
}

TEST(AppBundleInstall, IncomingPathIsHiddenSiblingInImagesDir)
{
    EXPECT_EQ(fs::incoming_app_image_path("/data/app/images"),
              "/data/app/images/.incoming.squashfs");
}

TEST(AppBundleInstall, ActivateRenamesIncomingToInactiveSlot)
{
    const auto dir = make_temp_dir("activate-a");
    write_incoming_set(dir, "new-app-payload");

    const std::string target = fs::activate_incoming_app_image(dir.string(), 'A');

    EXPECT_EQ(target, (dir / "app_b.squashfs").string());
    EXPECT_FALSE(std::filesystem::exists(dir / ".incoming.squashfs"));
    EXPECT_EQ(read_file(dir / "app_b.squashfs"), "new-app-payload");

    std::filesystem::remove_all(dir);
}

// The 3 sidecars must move alongside the squashfs, under the SAME renamed
// (image-keyed) name the runtime's mount verb looks them up by.
TEST(AppBundleInstall, ActivateRenamesAllThreeSidecarsAlongsideTheImage)
{
    const auto dir = make_temp_dir("activate-sidecars");
    write_incoming_set(dir, "payload");

    (void)fs::activate_incoming_app_image(dir.string(), 'A');

    for (const char* suffix : fs::kAppImageSidecarSuffixes) {
        const auto incoming_sidecar = dir / (".incoming.squashfs" + std::string(suffix));
        const auto target_sidecar = dir / ("app_b.squashfs" + std::string(suffix));
        EXPECT_FALSE(std::filesystem::exists(incoming_sidecar)) << suffix;
        ASSERT_TRUE(std::filesystem::exists(target_sidecar)) << suffix;
        EXPECT_EQ(read_file(target_sidecar), "payload" + std::string(suffix));
    }

    std::filesystem::remove_all(dir);
}

TEST(AppBundleInstall, ActivateTargetsSlotAWhenBIsCurrent)
{
    const auto dir = make_temp_dir("activate-b");
    write_incoming_set(dir, "payload");

    const std::string target = fs::activate_incoming_app_image(dir.string(), 'B');

    EXPECT_EQ(target, (dir / "app_a.squashfs").string());
    EXPECT_TRUE(std::filesystem::exists(dir / "app_a.squashfs"));

    std::filesystem::remove_all(dir);
}

TEST(AppBundleInstall, ActivateOverwritesPreviousInactiveImage)
{
    const auto dir = make_temp_dir("activate-overwrite");
    write_file(dir / "app_b.squashfs", "old-payload");
    write_incoming_set(dir, "new-payload");

    (void)fs::activate_incoming_app_image(dir.string(), 'A');

    EXPECT_EQ(read_file(dir / "app_b.squashfs"), "new-payload");

    std::filesystem::remove_all(dir);
}

// A missing staged image means the install hook never ran or wrote
// elsewhere — activation must fail loudly, not flip the boot variable.
TEST(AppBundleInstall, ActivateWithoutIncomingThrows)
{
    const auto dir = make_temp_dir("activate-missing");

    EXPECT_THROW((void)fs::activate_incoming_app_image(dir.string(), 'A'),
                 fs::GenericException);

    std::filesystem::remove_all(dir);
}

// A missing sidecar must be caught by the precheck, before any file moves —
// the runtime refuses to mount without all 3 sidecars anyway, so an
// incomplete stage is an install-time error, not a boot-time surprise.
TEST(AppBundleInstall, ActivateWithMissingSidecarThrowsAndTouchesNothing)
{
    const auto dir = make_temp_dir("activate-missing-sidecar");
    write_file(dir / ".incoming.squashfs", "payload");
    write_file(dir / ".incoming.squashfs.verity", "verity");
    // .roothash and .roothash.p7s deliberately absent.

    EXPECT_THROW((void)fs::activate_incoming_app_image(dir.string(), 'A'),
                 fs::GenericException);

    // Precheck runs before any rename: the (incomplete) incoming set is
    // still exactly where it was, nothing partially moved to app_b.squashfs*.
    EXPECT_TRUE(std::filesystem::exists(dir / ".incoming.squashfs"));
    EXPECT_TRUE(std::filesystem::exists(dir / ".incoming.squashfs.verity"));
    EXPECT_FALSE(std::filesystem::exists(dir / "app_b.squashfs"));
    EXPECT_FALSE(std::filesystem::exists(dir / "app_b.squashfs.verity"));

    std::filesystem::remove_all(dir);
}

TEST(AppBundleInstall, RaucBundlePayloadSniffsSquashfsMagic)
{
    const auto dir = make_temp_dir("sniff");

    std::string rauc_bytes(64, '\0');
    std::memcpy(&rauc_bytes[0], "hsqs", 4);
    write_file(dir / "app.raucb", rauc_bytes);

    std::string legacy_bytes(64, '\0');
    legacy_bytes[11] = 0x01; // big-endian version field @8 == 1 -> F&S app image
    write_file(dir / "legacy.app", legacy_bytes);

    EXPECT_TRUE(fs::is_rauc_bundle_payload((dir / "app.raucb").string()));
    EXPECT_FALSE(fs::is_rauc_bundle_payload((dir / "legacy.app").string()));

    std::filesystem::remove_all(dir);
}

TEST(AppBundleInstall, UnreadablePayloadThrows)
{
    EXPECT_THROW((void)fs::is_rauc_bundle_payload("/tmp/fs-updater-no-such-payload-987"),
                 fs::GenericException);
}

} // namespace
