// Standalone TU for fs::inspect_bundle so unit tests can build just
// this implementation + UpdateContainerReader without pulling fsupdate.cpp's
// libubootenv / libarchive / RAUC transitive dependencies.

#include "inspect_bundle.h"

#include "UpdateContainerReader.h"

#include <sys/stat.h>
#include <cstdint>
#include <string>

fs::BundleInfo fs::inspect_bundle(std::string_view path) noexcept
{
    BundleInfo info{};

    /* string_view is not guaranteed null-terminated; materialise a
     * std::string for the C-style APIs (::stat, UpdateContainerReader). */
    const std::string path_str(path);

    struct stat st{};
    if (::stat(path_str.c_str(), &st) != 0) {
        return info; /* valid stays false */
}

    info.valid = true;
    info.size  = static_cast<std::uint64_t>(st.st_size);

    /* Reading the v2.0 header + JSON descriptor can throw on any number
     * of conditions: short reads, malformed magic, v1.0 bundle, missing
     * fields. Inspection is metadata-only, so swallow everything and
     * leave update_type/version empty — callers see valid=true and a
     * size, and can proceed to install (which will re-validate). */
    try {
        UpdateContainerReader reader(path_str);
        reader.open();
        const Descriptor &desc = reader.descriptor();

        bool has_fw  = false;
        bool has_app = false;
        for (const Member &m : desc.members) {
            if      (m.type == MemberType::Firmware) {    has_fw  = true;
            } else if (m.type == MemberType::Application) { has_app = true;
}
        }

        if      (has_fw && has_app) { info.update_type = "fw+app";
        } else if (has_fw) {            info.update_type = "fw";
        } else if (has_app) {           info.update_type = "app";
}

        if      (!desc.version.empty()) {    info.version = desc.version;
        } else if (info.update_type == "fw"  && !desc.fw_version.empty()) {  info.version = desc.fw_version;
        } else if (info.update_type == "app" && !desc.app_version.empty()) { info.version = desc.app_version;
}
    } catch (...) {
        /* Not a v2.0 bundle, or partial container. valid+size stand. */
    }

    return info;
}
