#include <gtest/gtest.h>

#include "handle_update/app_bundle_install.h"
#include "handle_update/fs_exceptions.h"
#include "uboot_interface/allowed_uboot_variable_states.h"

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

TEST(AppSlotImagePath, ResolvesBothSlotLetters)
{
    EXPECT_EQ(fs::app_slot_image_path("/data/app/images", 'A'), "/data/app/images/app_a.squashfs");
    EXPECT_EQ(fs::app_slot_image_path("/data/app/images", 'B'), "/data/app/images/app_b.squashfs");
}

TEST(AppSlotImagePath, ToleratesTrailingSlash)
{
    /* STANDARD_APP_IMG_STORE conventionally carries a trailing slash; the
     * naming authority must resolve the same path either way. */
    EXPECT_EQ(fs::app_slot_image_path("/data/app/images/", 'A'), "/data/app/images/app_a.squashfs");
}

TEST(AppSlotImagePath, RejectsInvalidSlotLetter)
{
    EXPECT_THROW((void)fs::app_slot_image_path("/data/app/images", 'C'), fs::GenericException);
    EXPECT_THROW((void)fs::app_slot_image_path("/data/app/images", 'a'), fs::GenericException);
}

TEST(AppSlotProvisioned, TrueOnlyWhenSlotImageExists)
{
    const auto dir = make_temp_dir("provisioned");
    write_file(dir / "app_a.squashfs", "payload");

    EXPECT_TRUE(fs::app_slot_provisioned(dir.string(), 'A'));
    EXPECT_FALSE(fs::app_slot_provisioned(dir.string(), 'B'));

    std::filesystem::remove_all(dir);
}

TEST(ClassifyAppSlotSwitch, FullDigitByProvisionedMatrix)
{
    /* Unprovisioned wins over every digit value: with no image file, the
     * digit carries no usable history for that slot. */
    for (const int digit : {STATE_UPDATE_COMMITED, STATE_UPDATE_UNCOMMITED, STATE_UPDATE_BAD,
                            STATE_UPDATE_UNCOMMITED | STATE_UPDATE_BAD}) {
        EXPECT_EQ(fs::classify_app_slot_switch(digit, false),
                  fs::AppSlotSwitchVerdict::RefusedUnprovisioned)
            << "digit=" << digit;
    }

    EXPECT_EQ(fs::classify_app_slot_switch(STATE_UPDATE_COMMITED, true),
              fs::AppSlotSwitchVerdict::Allowed);
    EXPECT_EQ(fs::classify_app_slot_switch(STATE_UPDATE_UNCOMMITED, true),
              fs::AppSlotSwitchVerdict::RefusedUncommitted);
    EXPECT_EQ(fs::classify_app_slot_switch(STATE_UPDATE_BAD, true),
              fs::AppSlotSwitchVerdict::RefusedBad);
    /* Both bits set: uncommitted takes precedence over bad. */
    EXPECT_EQ(fs::classify_app_slot_switch(STATE_UPDATE_UNCOMMITED | STATE_UPDATE_BAD, true),
              fs::AppSlotSwitchVerdict::RefusedUncommitted);
}

TEST(AppSlotSwitchRefusal, NamesTheSlotTheVerdictCameFrom)
{
    /* The refusal must name the slot whose digit produced it; committing the
     * running slot cannot resolve it. */
    for (const char target : {'A', 'B'}) {
        const std::string slot(1, target);

        EXPECT_EQ(fs::app_slot_switch_refusal(fs::AppSlotSwitchVerdict::RefusedUnprovisioned, target),
                  "slot " + slot + " was never provisioned");
        EXPECT_EQ(fs::app_slot_switch_refusal(fs::AppSlotSwitchVerdict::RefusedUncommitted, target),
                  "slot " + slot + " is not committed");
        EXPECT_EQ(fs::app_slot_switch_refusal(fs::AppSlotSwitchVerdict::RefusedBad, target),
                  "slot " + slot + " is marked bad");
    }
}

TEST(AppSlotSwitchRefusal, TellsTheThreeRefusalsApart)
{
    /* Naming the slot is half of it; the other half is that the three reasons
     * stay distinguishable. A caller can only act on the reason -- provision
     * the slot, commit it, or install over it -- so a message shared between
     * two verdicts would be as unhelpful as the wrong slot. */
    const std::string unprovisioned =
        fs::app_slot_switch_refusal(fs::AppSlotSwitchVerdict::RefusedUnprovisioned, 'B');
    const std::string uncommitted =
        fs::app_slot_switch_refusal(fs::AppSlotSwitchVerdict::RefusedUncommitted, 'B');
    const std::string bad = fs::app_slot_switch_refusal(fs::AppSlotSwitchVerdict::RefusedBad, 'B');

    EXPECT_NE(unprovisioned, uncommitted);
    EXPECT_NE(uncommitted, bad);
    EXPECT_NE(unprovisioned, bad);
}

TEST(AppSlotSwitchRefusal, HasNothingToSayAboutAnAllowedSwitch)
{
    /* Empty rather than a placeholder: a caller that prints this
     * unconditionally shows nothing, instead of a sentence that reads like a
     * refusal for a switch that was permitted. */
    EXPECT_TRUE(fs::app_slot_switch_refusal(fs::AppSlotSwitchVerdict::Allowed, 'A').empty());
    EXPECT_TRUE(fs::app_slot_switch_refusal(fs::AppSlotSwitchVerdict::Allowed, 'B').empty());
}

} // namespace
