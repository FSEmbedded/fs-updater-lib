#pragma once

#include <fus_updater_lib/config.h> // FUS_LIB_RAUC_SCRATCH

namespace fs {
    // Use inline constexpr so it's header-only and avoids ODR violations
    inline constexpr char FSUPDATE_DOMAIN[] = "fsupdate";

    /// v2.0 streaming reader: where the RAUC firmware bundle is staged on
    /// persistent storage before `rauc install` is invoked. Default
    /// `/rw_fs/.cache/update.fw`; cmake-overridable via -DFSUP_RAUC_SCRATCH=...
    /// (the BSP recipe sets this), runtime-overridable via the
    /// `--rauc_scratch_path` CLI flag.
    inline constexpr const char* DEFAULT_RAUC_SCRATCH_PATH = FUS_LIB_RAUC_SCRATCH;

    /* use 8KB buffer size */
    inline constexpr size_t BUFFER_SIZE = 8192;
    /* use 64 KB buffer size for stream reading */
    inline constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024;
}