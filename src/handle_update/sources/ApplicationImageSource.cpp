#include "ApplicationImageSource.h"

#include <utility>

namespace fs {

ApplicationImageSource::ApplicationImageSource(std::string path)
    : path_(std::move(path))
{
}

UpdateArtifacts ApplicationImageSource::prepare(const StagingContext& /*ctx*/)
{
    // A raw application image is the application artifact as-is; no extraction
    // or staging. The application engine validates the header/CRC32 on open.
    UpdateArtifacts artifacts;
    artifacts.application = path_;
    return artifacts;
}

} // namespace fs
