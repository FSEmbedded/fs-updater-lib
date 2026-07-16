#include "RaucBundleSource.h"

#include "../fs_exceptions.h"

#include <cerrno>
#include <exception>
#include <utility>

namespace fs {

bool is_rauc_app_compatible(std::string_view compatible) noexcept
{
    constexpr std::string_view suffix = "-appfs";
    return compatible.size() >= suffix.size() &&
           compatible.substr(compatible.size() - suffix.size()) == suffix;
}

RaucBundleSource::RaucBundleSource(std::string path, RaucCompatibleProvider compatible_provider)
    : path_(std::move(path))
    , compatible_provider_(std::move(compatible_provider))
{
}

UpdateArtifacts RaucBundleSource::prepare(const StagingContext& /*ctx*/)
{
    UpdateArtifacts artifacts;

    if (!compatible_provider_) {
        // No manifest access: a raw .raucb is the firmware artifact as-is.
        artifacts.firmware = path_;
        return artifacts;
    }

    std::string compatible;
    try {
        compatible = compatible_provider_(path_);
    } catch (const std::exception& e) {
        // Rauc* exceptions derive from std::exception, not GenericException;
        // rewrap so prepare() keeps the front door's error contract.
        throw GenericException(
            "RaucBundleSource: cannot read manifest of '" + path_ + "': " + e.what(), EIO);
    }

    if (is_rauc_app_compatible(compatible)) {
        artifacts.application = path_;
    } else {
        artifacts.firmware = path_;
    }
    return artifacts;
}

} // namespace fs
