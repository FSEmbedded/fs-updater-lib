#include "UpdateContainerSource.h"

#include "../Descriptor.h"
#include "../UpdateContainerReader.h"
#include "../UpdateStreamSink.h"
#include "util/posix_utils.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace fs {

namespace {
// Canonical staging filenames the downstream engines read, matching the
// legacy UpdateStore convention.
constexpr const char* kFirmwareStoreName    = "update.fw";
constexpr const char* kApplicationStoreName = "update.app";
} // namespace

UpdateContainerSource::UpdateContainerSource(std::string path)
    : path_(std::move(path))
{
}

UpdateArtifacts UpdateContainerSource::prepare(const StagingContext& ctx)
{
    UpdateContainerReader reader(path_);
    reader.open();
    const auto& members = reader.descriptor().members;

    // Byte-weighted progress denominator: only members that actually reach
    // disk (firmware / application) count, matching the displayed ratio.
    std::uint64_t total_bytes = 0;
    for (const auto& m : members) {
        if (m.type == MemberType::Firmware || m.type == MemberType::Application) {
            total_bytes += m.size;
        }
    }

    UpdateArtifacts artifacts;
    std::uint64_t   bytes_done = 0;

    std::function<void(std::uint64_t)> on_chunk;
    if (ctx.on_progress && total_bytes > 0) {
        on_chunk = [&](std::uint64_t chunk_bytes) {
            ctx.on_progress(bytes_done + chunk_bytes, total_bytes);
        };
    }

    for (const auto& member : members) {
        if (member.type == MemberType::Firmware) {
            const std::string dest = util::path_join(ctx.staging_dir, kFirmwareStoreName);
            FileSink sink(dest);
            reader.extract(member, sink, on_chunk);
            artifacts.firmware = dest;
            bytes_done += member.size;
        } else if (member.type == MemberType::Application) {
            const std::string dest = util::path_join(ctx.staging_dir, kApplicationStoreName);
            FileSink sink(dest);
            reader.extract(member, sink, on_chunk);
            artifacts.application = dest;
            bytes_done += member.size;
        }
        // Other member types (manifest, future variants) are skipped for
        // forward-compat.
    }

    return artifacts;
}

} // namespace fs
