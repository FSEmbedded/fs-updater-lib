#include "RaucApplicationUpdate.h"

#include "../dbus/rauc_dbus_client.h"

namespace updater {

void RaucApplicationUpdate::install_bundle_via_rauc(const std::string& path_to_bundle) {
    rauc::rauc_dbus_client rauc_client(uboot_handler, logger);
    rauc_client.installBundle(path_to_bundle);
    rauc_client.waitForCompletion(0, progress_cb_);
}

} // namespace updater
