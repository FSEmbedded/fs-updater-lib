#pragma once

#include "updateApplication.h"

namespace updater {

// Production applicationUpdate: implements the RAUC D-Bus install seam that
// applicationUpdate leaves pure virtual. Own TU so the target-only
// environment library stays out of applicationUpdate's other methods.
class RaucApplicationUpdate final : public applicationUpdate {
public:
    using applicationUpdate::applicationUpdate;

protected:
    void install_bundle_via_rauc(const std::string& path_to_bundle) override;
};

} // namespace updater
