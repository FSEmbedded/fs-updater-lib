#pragma once

#include <filesystem>
#include <string>

#include "fs_consts.h"

namespace fs
{
/// Resolve the v2.0 staging directory from an optional scratch-file override.
/// Empty override → parent of DEFAULT_RAUC_SCRATCH_PATH; otherwise parent of
/// the supplied path. The parent dir is where v2.0 stages firmware/app members
/// before `rauc install` is invoked.
inline std::filesystem::path resolve_scratch_dir(const std::string &override_path)
{
    const std::filesystem::path scratch_file = override_path.empty()
        ? std::filesystem::path(DEFAULT_RAUC_SCRATCH_PATH)
        : std::filesystem::path(override_path);
    return scratch_file.parent_path();
}
}
