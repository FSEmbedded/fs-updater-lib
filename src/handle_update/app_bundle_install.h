#pragma once

#include <array>
#include <string>

namespace fs {

/**
 * Staging path the RAUC install hook writes the new application image to: a
 * hidden sibling inside the image store, so activation is a same-filesystem
 * rename with a single directory fsync. Single source of the hook contract
 * (the hook script in the layer must write exactly this path, plus the 3
 * verity-sidecar suffixes below appended to it).
 */
[[nodiscard]] std::string incoming_app_image_path(const std::string& images_dir);

/**
 * Verity-sidecar suffixes appended to the incoming/activated squashfs's own
 * filename (e.g. ".../app_a.squashfs.verity") — the 4-file contract shared
 * by the container app-image build, the RAUC install hook, and
 * activate_incoming_app_image() below. Not configurable: all parties must
 * agree on the same fixed set.
 */
inline constexpr std::array<const char*, 3> kAppImageSidecarSuffixes = {".verity", ".roothash",
                                                                          ".roothash.p7s"};

/**
 * True when the payload sniffs as a raw RAUC bundle (squashfs magic) — the
 * app engine then installs via RAUC instead of the F&S image path. Throws
 * fs::GenericException when the payload cannot be read.
 */
[[nodiscard]] bool is_rauc_bundle_payload(const std::string& path);

/**
 * Atomically activate the staged application image SET (the squashfs plus
 * its 3 verity sidecars, kAppImageSidecarSuffixes above): rename
 * <images_dir>/.incoming.squashfs{,.verity,.roothash,.roothash.p7s} onto the
 * inactive slot's file set (app_b.squashfs* while 'A' is current, app_a.squashfs*
 * while 'B') and fsync the directory once. Returns the activated squashfs
 * target path.
 *
 * All 4 staged files are required — checked up front, before any rename —
 * and renamed sidecars-first, squashfs-last: the squashfs rename is the
 * effective commit point, so a failure partway through leaves the previous
 * slot's complete set untouched. Throws fs::GenericException when any staged
 * file is missing or a rename fails — the boot variable must not flip in
 * that case.
 */
std::string activate_incoming_app_image(const std::string& images_dir, char current_app);

/**
 * Single naming authority for a slot's image file: 'A' -> app_a.squashfs,
 * 'B' -> app_b.squashfs. A trailing '/' on images_dir is tolerated. Any
 * other slot letter is an invariant violation and throws
 * fs::GenericException(EINVAL).
 */
[[nodiscard]] std::string app_slot_image_path(const std::string& images_dir, char slot);

/**
 * A slot is provisioned iff its squashfs exists. Presence of the squashfs
 * (the last file activate_incoming_app_image renames into place) implies a
 * complete install; sidecars are not required here.
 */
[[nodiscard]] bool app_slot_provisioned(const std::string& images_dir, char slot);

enum class AppSlotSwitchVerdict : unsigned char
{
    Allowed,
    RefusedUnprovisioned, /* no image was ever installed to the target slot */
    RefusedUncommitted,   /* STATE_UPDATE_UNCOMMITED set on the target digit */
    RefusedBad            /* STATE_UPDATE_BAD set on the target digit */
};

/**
 * Pure decision core for a committed-state app-slot switch. Unprovisioned
 * wins over the digit bits: with no image file, the digit carries no usable
 * history for that slot.
 */
[[nodiscard]] AppSlotSwitchVerdict classify_app_slot_switch(int target_state_digit,
                                                            bool target_provisioned);

/**
 * How a refused switch is put to the operator. Lives here, next to the verdict
 * it describes, so the slot named is the slot the verdict was computed from,
 * never the running slot (committing the running slot cannot resolve the
 * refusal).
 *
 * @param verdict A refusal. Allowed has no message and yields an empty string.
 * @param target_slot 'A' or 'B' — the slot whose digit produced the verdict.
 */
[[nodiscard]] std::string app_slot_switch_refusal(AppSlotSwitchVerdict verdict, char target_slot);

} // namespace fs
