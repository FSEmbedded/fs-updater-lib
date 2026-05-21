#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fs {

/**
 * Metadata read from a v2.0 update bundle by inspect_bundle().
 *
 * Populated without committing to an install — opens the file, reads
 * the F&S header, parses the JSON descriptor, then closes. Used by
 * the D-Bus service to populate UpdateType/UpdateVersion/UpdateSize
 * properties on InstallLocal before the install worker starts, so
 * subscribers see meaningful values during the install.
 *
 * Field semantics:
 *  - valid       false → stat() failed (ENOENT, EACCES, …). Other
 *                fields are zero/empty and not meaningful.
 *                true  → stat() succeeded. The file exists and `size`
 *                is set; whether it is a v2.0 container is reported
 *                via `update_type` / `version` being populated.
 *  - update_type "fw"     — descriptor contains only Firmware members
 *                "app"    — descriptor contains only Application members
 *                "fw+app" — descriptor contains both
 *                ""       — file isn't a recognized v2.0 bundle, or
 *                           the bundle had no Firmware/Application
 *                           members. The install can still be
 *                           attempted and may still fail at the worker.
 *  - version     Bundle version string. Precedence: descriptor.version,
 *                falling back to fw_version (for "fw"), app_version
 *                (for "app"), or empty when the descriptor wasn't
 *                readable.
 *  - size        File size in bytes (from stat).
 *
 * `valid=true` with empty update_type does NOT imply the file is
 * installable — the install worker re-validates the header and may
 * still fail. inspect_bundle is metadata-only.
 */
struct BundleInfo
{
    bool          valid{false};
    std::string   update_type;
    std::string   version;
    std::uint64_t size{0};
};

/**
 * Read metadata from a v2.0 update bundle without installing it.
 *
 * Opens @p path, runs the v2.0 header + descriptor parse, then
 * closes. Cheap: O(header + descriptor); does not stream any member
 * bytes.
 *
 * Never throws — all errors are reported via the returned BundleInfo
 * (see the struct doc for field semantics). Safe to call from
 * callbacks that must not throw across language/ABI boundaries.
 *
 * Free function (no FSUpdate state needed): callers can introspect a
 * bundle without claiming U-Boot or any other FSUpdate resources;
 * unit tests can exercise it directly.
 *
 * @param path Path to the bundle on disk.
 * @return BundleInfo with valid/update_type/version/size populated.
 */
[[nodiscard]] BundleInfo inspect_bundle(std::string_view path) noexcept;

} // namespace fs
