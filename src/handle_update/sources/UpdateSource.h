#pragma once

#include "../fs_exceptions.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>

namespace fs {

/**
 * Discriminator over every input format the updater's front door accepts.
 *
 * One enum for all entry formats (container, raw RAUC bundle, raw F&S
 * application image), plus the deferred legacy tarball seam. Replaces the
 * container-only `FormatVersion` as the unifying format axis.
 */
enum class UpdateFormat : std::uint8_t {
    Unknown = 0,
    Container,        ///< F&S v2.0 streaming container ("FSLX" + version 0x20 + "FSUPv2")
    LegacyTarball,    ///< F&S v1.0 container ("FSLX" + version 0x10) — detection only, deferred
    RaucBundle,       ///< raw RAUC bundle (squashfs "hsqs" magic)
    ApplicationImage, ///< raw F&S application image (no magic; header version field == 1)
};

/**
 * Non-owning view of the leading bytes of an update artifact. The caller
 * reads a fixed, bounded number of leading bytes once and hands them here;
 * detection performs no I/O of its own.
 */
struct FormatProbe {
    const unsigned char* data;
    std::size_t          size;
};

/**
 * Pure, deterministic content sniff: map the leading bytes to an
 * `UpdateFormat`. No I/O, no allocation, no locale/extension/time influence
 * — identical input always yields the identical format.
 *
 * Probe order (offset-0 magics first, version-field fallback last):
 *   1. "FSLX" @0  -> Container (version 0x20 + type "FSUPv2") or
 *                    LegacyTarball (version 0x10); else Unknown
 *   2. "hsqs" @0  -> RaucBundle (squashfs)
 *   3. version field @8 (4-byte big-endian) == 1 -> ApplicationImage
 *   4. otherwise  -> Unknown (also for null/short probes)
 *
 * The application-image branch is a *routing* sniff only. Header CRC
 * integrity is intentionally NOT checked here; it is validated downstream by
 * `applicationImage`'s constructor (the single source of CRC truth) before
 * any slot is written. A file that sniffs as ApplicationImage but is corrupt
 * is rejected there, deterministically, before any destructive step.
 *
 * NOTE: the "FSLX" rules mirror `detect_format_version` / the fs_header
 * layout in UpdateContainerReader.h; consolidate when that container-only
 * detector is retired.
 */
[[nodiscard]] inline UpdateFormat detect_update_format(const FormatProbe& probe) noexcept
{
    const unsigned char* const d = probe.data;
    const std::size_t          n = probe.size;

    if (d == nullptr) {
        return UpdateFormat::Unknown;
    }

    // 1. F&S container family — "FSLX" magic at offset 0.
    if (n >= 4 && std::memcmp(d, "FSLX", 4) == 0) {
        if (n < 16) {
            return UpdateFormat::Unknown; // header too short to read the version byte
        }
        const unsigned char version = d[15]; // fs_header_v0_0.version
        if (version == 0x10) {
            return UpdateFormat::LegacyTarball;
        }
        if (version == 0x20) {
            // v2.0 is only a container when the type tag confirms it.
            if (n >= 22 && std::memcmp(d + 16, "FSUPv2", 6) == 0) {
                return UpdateFormat::Container;
            }
            return UpdateFormat::Unknown;
        }
        return UpdateFormat::Unknown;
    }

    // 2. Raw RAUC bundle — squashfs "hsqs" magic at offset 0.
    if (n >= 4 && std::memcmp(d, "hsqs", 4) == 0) {
        return UpdateFormat::RaucBundle;
    }

    // 3. Raw F&S application image — no magic; 4-byte big-endian version
    //    field at offset 8 must equal 1. CRC is validated downstream.
    if (n >= 12) {
        const std::uint32_t version =
            (static_cast<std::uint32_t>(d[8])  << 24) |
            (static_cast<std::uint32_t>(d[9])  << 16) |
            (static_cast<std::uint32_t>(d[10]) << 8)  |
            (static_cast<std::uint32_t>(d[11]));
        if (version == 1u) {
            return UpdateFormat::ApplicationImage;
        }
    }

    return UpdateFormat::Unknown;
}

/**
 * Normalised result of resolving an update artifact: the firmware and/or
 * application payloads ready for the engine dispatch, each as a path into
 * the staging area (or the original file for raw single-payload inputs).
 * Replaces the old `UpdateStore`'s two bool flags.
 */
struct UpdateArtifacts {
    std::optional<std::filesystem::path> firmware;
    std::optional<std::filesystem::path> application;
};

/**
 * Which engine the resolved artifacts dispatch to. The numeric values match
 * the legacy `installed_update_type` contract (1 fw, 2 app, 3 combined) so
 * the front door can report them unchanged.
 */
enum class DispatchKind : std::uint8_t {
    Firmware               = 1,
    Application            = 2,
    FirmwareAndApplication = 3,
};

/**
 * Map resolved artifacts to their dispatch kind. Pure decision logic,
 * unit-testable without the engines or U-Boot. Throws
 * `GenericException(EPERM)` for the empty set.
 */
[[nodiscard]] inline DispatchKind classify_dispatch(const UpdateArtifacts& artifacts)
{
    const bool has_fw  = artifacts.firmware.has_value();
    const bool has_app = artifacts.application.has_value();

    if (has_fw && has_app) {
        return DispatchKind::FirmwareAndApplication;
    }
    if (has_fw) {
        return DispatchKind::Firmware;
    }
    if (has_app) {
        return DispatchKind::Application;
    }
    throw GenericException("update_image: Invalid update: no firmware or application payload", EPERM);
}

/**
 * Inputs an `UpdateSource` needs to resolve its artifacts: the staging
 * directory members are written into, and an optional progress hook called
 * with cumulative (bytes_done, bytes_total) during extraction. The caller
 * maps the raw byte ratio onto whatever progress band it presents — the
 * source does not bake in any percentage scaling.
 */
struct StagingContext {
    std::filesystem::path staging_dir;
    std::function<void(std::uint64_t bytes_done, std::uint64_t bytes_total)> on_progress;
};

/**
 * Abstract update source: resolves a concrete input artifact into normalised
 * `UpdateArtifacts` for the engine dispatch. One derivation per input format
 * (container, raw RAUC bundle, raw application image, legacy seam). Following
 * the lib convention (`updateBase`, `UpdateStreamSink`) the base carries no
 * `I` prefix; `IUpdater` is the service-side DI seam, not mirrored here.
 */
class UpdateSource {
public:
    virtual ~UpdateSource() = default;

    UpdateSource(const UpdateSource&)            = delete;
    UpdateSource& operator=(const UpdateSource&) = delete;
    UpdateSource(UpdateSource&&)                 = delete;
    UpdateSource& operator=(UpdateSource&&)      = delete;

    /**
     * Resolve this source's payloads into the staging area and return the
     * firmware/application paths. Throws fs::GenericException on any failure.
     */
    [[nodiscard]] virtual UpdateArtifacts prepare(const StagingContext& ctx) = 0;

protected:
    UpdateSource() = default;
};

} // namespace fs
