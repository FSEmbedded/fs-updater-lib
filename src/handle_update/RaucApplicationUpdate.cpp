#include "RaucApplicationUpdate.h"

#include "../dbus/rauc_dbus_client.h"
#include "../uboot_interface/UBoot.h"

#include <stdexcept>
#include <typeinfo>

namespace updater {

void RaucApplicationUpdate::install_bundle_via_rauc(const std::string& path_to_bundle) {
    /* uboot_handler is IUBootEnv-typed for testability; production always
     * constructs this class over the concrete UBoot::UBoot (fsupdate.cpp),
     * which rauc_dbus_client requires. */
    auto concrete_uboot = std::dynamic_pointer_cast<UBoot::UBoot>(uboot_handler);
    if (!concrete_uboot) {
        throw std::bad_cast();
    }
    rauc::rauc_dbus_client rauc_client(concrete_uboot, logger);
    rauc_client.installBundle(path_to_bundle);
    rauc_client.waitForCompletion(0, progress_cb_);
}

} // namespace updater
