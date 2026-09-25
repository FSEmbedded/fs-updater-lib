#pragma once

/* if not defined in configuration file set to default value */
#ifndef TEMP_ADU_WORK_DIR
    #define TEMP_ADU_WORK_DIR "/tmp/adu/.work"
#endif

namespace fs {
    // Use inline constexpr so it's header-only and avoids ODR violations
    inline constexpr char FSUPDATE_DOMAIN[] = "fsupdate";

    inline constexpr const char* TARGET_ARCHIV_DIR_PATH = "/tmp/adu/.update";

    /* Written into the work directory once an install has completed. It is
     * the install-progress state the update agent polls and the oracle that
     * keeps a rollback from running before the reboot: the directory has to
     * live on volatile storage, and nothing may remove the file before one.
     */
    inline constexpr const char *UPDATE_INSTALLED_MARKER = "updateInstalled";
    inline constexpr const char *UPDATE_INSTALLED_MARKER_PATH = TEMP_ADU_WORK_DIR "/updateInstalled";

    /* use 8KB buffer size */
    inline constexpr size_t BUFFER_SIZE = 8192;
    /* use 64 KB buffer size for stream reading */
    inline constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024;
}