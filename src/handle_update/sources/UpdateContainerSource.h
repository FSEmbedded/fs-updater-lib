#pragma once

#include "UpdateSource.h"

#include <filesystem>

namespace fs {

/**
 * Update source for the F&S streaming container. Wraps UpdateContainerReader:
 * `prepare()` opens the container, streams its firmware/application members
 * into the staging directory under their canonical names (`update.fw` /
 * `update.app`), and returns the resolved `UpdateArtifacts`. Unknown member
 * types are skipped for forward compatibility.
 */
class UpdateContainerSource : public UpdateSource {
public:
    explicit UpdateContainerSource(std::filesystem::path path);

    [[nodiscard]] UpdateArtifacts prepare(const StagingContext& ctx) override;

private:
    std::filesystem::path path_;
};

} // namespace fs
