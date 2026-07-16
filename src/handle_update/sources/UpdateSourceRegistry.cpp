#include "UpdateSourceRegistry.h"

#include "ApplicationImageSource.h"
#include "RaucBundleSource.h"
#include "UpdateContainerSource.h"
#include "../fs_exceptions.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <fstream>

namespace fs {

namespace {
// Enough leading bytes to cover every probe: the F&S container header needs
// the version byte (offset 15) plus the type tag (offsets 16..21); 64 bytes
// is the full fs_header and a generous bound for the others.
constexpr std::size_t kProbeBytes = 64;
} // namespace

std::unique_ptr<UpdateSource> make_update_source(const std::string& path,
                                                 RaucCompatibleProvider rauc_compatible)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        throw GenericException("make_update_source: cannot open '" + path + "'",
                               errno != 0 ? errno : ENOENT);
    }

    std::array<unsigned char, kProbeBytes> buf{};
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    const auto n = static_cast<std::size_t>(in.gcount());

    const FormatProbe probe{buf.data(), n};

    switch (detect_update_format(probe)) {
        case UpdateFormat::Container:
            return std::make_unique<UpdateContainerSource>(path);

        case UpdateFormat::RaucBundle:
            return std::make_unique<RaucBundleSource>(path, std::move(rauc_compatible));
        case UpdateFormat::ApplicationImage:
            return std::make_unique<ApplicationImageSource>(path);
        case UpdateFormat::LegacyTarball:
            throw UpdateFormatNotSupported("legacy v1.0 tarball container");

        case UpdateFormat::Unknown:
        default:
            throw UnknownUpdateFormat(path);
    }
}

} // namespace fs
