#pragma once

#include "updateApplication.h"

namespace updater {

// Production applicationUpdate: implements the RAUC D-Bus install seam that
// applicationUpdate leaves pure virtual. Kept in its own translation unit
// (own .cpp, pulling in rauc_dbus_client + the concrete UBoot::UBoot/
// libubootenv dependency) so applicationUpdate's other methods stay linkable
// in the lightweight native test build, which excludes that dependency.
class RaucApplicationUpdate final : public applicationUpdate {
public:
    using applicationUpdate::applicationUpdate;

protected:
    void install_bundle_via_rauc(const std::string& path_to_bundle) override;
};

} // namespace updater
