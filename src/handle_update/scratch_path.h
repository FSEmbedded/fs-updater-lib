#pragma once

#include <string>

#include "fs_consts.h"

namespace fs
{
/// Resolve the v2.0 staging directory from an optional scratch-file override.
/// Empty override → parent of DEFAULT_RAUC_SCRATCH_PATH; otherwise parent of
/// the supplied path. The parent dir is where v2.0 stages firmware/app members
/// before `rauc install` is invoked.
inline std::string resolve_scratch_dir(const std::string &override_path)
{
    const std::string scratch_file =
        override_path.empty() ? std::string{DEFAULT_RAUC_SCRATCH_PATH} : override_path;
    // Parent directory = everything before the last '/'. Reproduces
    // std::filesystem::path::parent_path() for the absolute scratch paths used
    // here (root child → "/", no separator → "").
    const std::string::size_type sep = scratch_file.find_last_of('/');
    if (sep == std::string::npos) {
        return std::string{};
    }
    if (sep == 0) {
        return std::string{"/"};
    }
    return scratch_file.substr(0, sep);
}
}
