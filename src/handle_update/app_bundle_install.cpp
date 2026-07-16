#include "app_bundle_install.h"

#include "fs_exceptions.h"
#include "sources/UpdateSource.h" // detect_update_format — the single sniff authority
#include "util/posix_utils.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <fstream>

namespace fs {

std::string incoming_app_image_path(const std::string& images_dir)
{
    return images_dir + "/.incoming.squashfs";
}

bool is_rauc_bundle_payload(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        throw GenericException("is_rauc_bundle_payload: cannot open '" + path + "'",
                               errno != 0 ? errno : ENOENT);
    }

    std::array<unsigned char, 64> buf{};
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    const auto n = static_cast<std::size_t>(in.gcount());

    return detect_update_format(FormatProbe{buf.data(), n}) == UpdateFormat::RaucBundle;
}

std::string activate_incoming_app_image(const std::string& images_dir, char current_app)
{
    const std::string incoming = incoming_app_image_path(images_dir);

    std::string target = images_dir;
    target += (current_app == 'A') ? "/app_b.squashfs" : "/app_a.squashfs";

    // All 4 staged files must exist before ANY rename happens — an
    // incomplete set is refused up front rather than left half-renamed. The
    // runtime's mount verb fails closed on a missing sidecar anyway, so an
    // incomplete stage is an install-time error, not a boot-time surprise.
    if (!util::path_exists(incoming)) {
        throw GenericException("activate_incoming_app_image: staged image missing: '" + incoming + "'",
                               ENOENT);
    }
    for (const char* suffix : kAppImageSidecarSuffixes) {
        const std::string incoming_sidecar = incoming + suffix;
        if (!util::path_exists(incoming_sidecar)) {
            throw GenericException(
                "activate_incoming_app_image: staged sidecar missing: '" + incoming_sidecar + "'", ENOENT);
        }
    }

    // Rename order: sidecars first, the squashfs LAST. The squashfs rename is
    // the effective commit point (the runtime looks up sidecars by the
    // squashfs's own final name) — a failure partway through this loop
    // leaves the previous slot's complete, rollback-able set untouched, and
    // this function throws before the caller ever flips the boot variable.
    for (const char* suffix : kAppImageSidecarSuffixes) {
        const std::string incoming_sidecar = incoming + suffix;
        const std::string target_sidecar = target + suffix;
        if (!util::rename_file(incoming_sidecar, target_sidecar)) {
            throw GenericException("activate_incoming_app_image: cannot rename '" + incoming_sidecar +
                                       "' to '" + target_sidecar + "'",
                                   errno != 0 ? errno : EIO);
        }
    }

    if (!util::rename_file(incoming, target)) {
        throw GenericException("activate_incoming_app_image: cannot rename '" + incoming +
                                   "' to '" + target + "'",
                               errno != 0 ? errno : EIO);
    }

    // Make all 4 renames durable before the caller flips the boot variable.
    const int dir_fd = ::open(images_dir.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    return target;
}

} // namespace fs
