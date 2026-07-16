#pragma once

#include <string>

namespace fs {

/**
 * Staging path the RAUC install hook writes the new application image to: a
 * hidden sibling inside the image store, so activation is a same-filesystem
 * rename with a single directory fsync. Single source of the hook contract
 * (the hook script in the layer must write exactly this path).
 */
[[nodiscard]] std::string incoming_app_image_path(const std::string& images_dir);

/**
 * True when the payload sniffs as a raw RAUC bundle (squashfs magic) — the
 * app engine then installs via RAUC instead of the F&S image path. Throws
 * fs::GenericException when the payload cannot be read.
 */
[[nodiscard]] bool is_rauc_bundle_payload(const std::string& path);

/**
 * Atomically activate the staged application image: rename
 * <images_dir>/.incoming.squashfs onto the inactive slot file
 * (app_b.squashfs while 'A' is current, app_a.squashfs while 'B') and fsync
 * the directory. Returns the activated target path.
 *
 * Throws fs::GenericException when the staged image is missing or the
 * rename fails — the boot variable must not flip in that case.
 */
std::string activate_incoming_app_image(const std::string& images_dir, char current_app);

} // namespace fs
