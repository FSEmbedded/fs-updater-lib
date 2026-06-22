#pragma once

#include "UpdateSource.h"

#include <filesystem>

namespace fs {

/**
 * Update source for a raw RAUC bundle (`.raucb`, squashfs `hsqs` magic). The
 * bundle is firmware: `prepare()` hands the original path straight to the
 * firmware engine (`rauc install <bundle>`), with no staging copy — RAUC
 * reads the bundle from any path and validates it itself. This mirrors how
 * the explicit-update_type path already installs a raw payload directly.
 */
class RaucBundleSource : public UpdateSource {
public:
    explicit RaucBundleSource(std::filesystem::path path);

    [[nodiscard]] UpdateArtifacts prepare(const StagingContext& ctx) override;

private:
    std::filesystem::path path_;
};

} // namespace fs
