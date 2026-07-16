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

    if (!util::rename_file(incoming, target)) {
        throw GenericException("activate_incoming_app_image: cannot rename '" + incoming +
                                   "' to '" + target + "'",
                               errno != 0 ? errno : EIO);
    }

    // Make the rename durable before the caller flips the boot variable.
    const int dir_fd = ::open(images_dir.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    return target;
}

} // namespace fs
