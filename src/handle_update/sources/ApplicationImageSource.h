#pragma once

#include "UpdateSource.h"

#include <string>

namespace fs {

/**
 * Update source for a raw F&S application image (no offset-0 magic; header
 * version field == 1). The image is the application payload: `prepare()`
 * hands the original path straight to the application engine
 * (`update_application` → `applicationImage`), which validates the header and
 * CRC32 itself — no staging copy, and integrity is not duplicated here. This
 * mirrors how the explicit-update_type path already installs a raw payload.
 */
class ApplicationImageSource : public UpdateSource {
public:
    explicit ApplicationImageSource(std::string path);

    [[nodiscard]] UpdateArtifacts prepare(const StagingContext& ctx) override;

private:
    std::string path_;
};

} // namespace fs
