#include "RaucBundleSource.h"

#include <utility>

namespace fs {

RaucBundleSource::RaucBundleSource(std::string path)
    : path_(std::move(path))
{
}

UpdateArtifacts RaucBundleSource::prepare(const StagingContext& /*ctx*/)
{
    // A raw .raucb is the firmware artifact as-is; no extraction or staging.
    // The firmware engine runs `rauc install` directly on this path, and RAUC
    // validates the bundle. Staging context is unused (nothing is written).
    UpdateArtifacts artifacts;
    artifacts.firmware = path_;
    return artifacts;
}

} // namespace fs
