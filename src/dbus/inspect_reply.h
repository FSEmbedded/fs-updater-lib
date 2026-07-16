#pragma once

#include <systemd/sd-bus.h>

#include <string>

namespace rauc {

/**
 * Extract the manifest `compatible` string from a RAUC InspectBundle reply.
 *
 * The reply body is a nested a{sv}: the manifest identity lives in an inner
 * "update" dict ("compatible", "version", ...) — rauc r_manifest_to_dict().
 * The message cursor must sit at the root a{sv}; the walk consumes it.
 *
 * Returns the empty string when the reply carries no update dict, no
 * compatible entry, or an unexpectedly typed value; never throws.
 */
[[nodiscard]] std::string parse_inspect_bundle_compatible(sd_bus_message* reply) noexcept;

} // namespace rauc
