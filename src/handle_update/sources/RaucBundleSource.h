#pragma once

#include "UpdateSource.h"

#include <functional>
#include <string>
#include <string_view>

namespace fs {

/**
 * Reads the manifest `compatible` string of a RAUC bundle. Production wires
 * this to the RAUC D-Bus InspectBundle call; unit tests inject a fake. May
 * throw on failure (service unavailable, invalid bundle) — prepare() wraps
 * any such failure into fs::GenericException.
 */
using RaucCompatibleProvider = std::function<std::string(const std::string& bundle_path)>;

/**
 * Bundle-recipe convention: application bundles carry a manifest
 * `compatible` ending in "-appfs"; everything else is firmware.
 */
[[nodiscard]] bool is_rauc_app_compatible(std::string_view compatible) noexcept;

/**
 * Update source for a raw RAUC bundle (`.raucb`, squashfs `hsqs` magic).
 * Firmware and application bundles share the same byte format, so the
 * manifest `compatible` (via the injected provider) is the discriminator:
 * an app-compatible bundle resolves to the application artifact, everything
 * else is the firmware artifact. Without a provider the bundle is firmware
 * (the historical behaviour). Either way prepare() hands the original path
 * through — no staging copy; the engine runs RAUC on it and RAUC validates
 * the bundle itself.
 */
class RaucBundleSource : public UpdateSource {
public:
    explicit RaucBundleSource(std::string path,
                              RaucCompatibleProvider compatible_provider = nullptr);

    [[nodiscard]] UpdateArtifacts prepare(const StagingContext& ctx) override;

private:
    std::string            path_;
    RaucCompatibleProvider compatible_provider_;
};

} // namespace fs
