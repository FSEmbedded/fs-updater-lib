#pragma once

#include "UpdateSource.h"

#include <memory>
#include <string>

namespace fs {

/**
 * Factory / registry for update sources. Reads the leading bytes of `path`,
 * identifies the format via `detect_update_format`, and returns the matching
 * `UpdateSource`.
 *
 * Throws:
 *  - `fs::GenericException` if the file cannot be opened (ENOENT/EACCES) or
 *    its leading bytes cannot be read.
 *  - `fs::UpdateFormatNotSupported` (ENOSYS) for a recognised format whose
 *    source is not wired in this build (raw RAUC bundle, raw application
 *    image, legacy tarball — added in later slices).
 *  - `fs::UnknownUpdateFormat` (ENOTSUP) if the bytes match no known format.
 *
 * Adding a new update type is a pure extension here: detect it in
 * `detect_update_format`, then construct its source in this switch.
 */
[[nodiscard]] std::unique_ptr<UpdateSource> make_update_source(const std::string& path);

} // namespace fs
