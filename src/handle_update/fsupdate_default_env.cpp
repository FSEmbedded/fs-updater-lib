#include "fsupdate.h"

#include "../uboot_interface/UBoot.h"

#include <memory>

/* The only translation unit that names the concrete environment accessor.
 * Everything else in the outer update object works through the interface, so
 * the rest of it links without the target-only environment library and can be
 * driven from an in-memory environment in tests. Same split as the RAUC
 * install seam next door, and for the same reason.
 */
fs::FSUpdate::FSUpdate(const std::shared_ptr<logger::LoggerHandler> &ptr)
    : FSUpdate(std::make_shared<UBoot::UBoot>(UBOOT_CONFIG_PATH), ptr)
{
}
