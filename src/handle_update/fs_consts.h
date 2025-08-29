#pragma once

namespace fs {
    // Use inline constexpr so it's header-only and avoids ODR violations
    inline constexpr char FSUPDATE_DOMAIN[] = "fsupdate";

    inline constexpr const char* TARGET_ARCHIV_DIR_PATH      = "/tmp/adu/.update";
    inline constexpr const char* TARGET_ARCHIVE_UPDATE_STORE = "/tmp/adu/.update/tmp.tar.bz2";

    /* use 8KB buffer size */
    inline constexpr size_t BUFFER_SIZE = 8192;
    /* use 64 KB buffer size for stream reading */
    inline constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024;
}