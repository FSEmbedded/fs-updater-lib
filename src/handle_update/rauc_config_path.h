#pragma once

#include <string>
#include <vector>

#include "../util/posix_utils.h"
#include "updateApplication.h"

namespace fs
{
/// RAUC's own search order for its system configuration: the first path that
/// exists wins, and /etc comes first so a deliberate administrator override
/// keeps precedence. Diverging from RAUC's order would let the two verify
/// bundles against two different keyrings, and a split trust root is a worse
/// failure than a stale shared one.
///
/// This makes the two agree per resolution, not for all time: RAUC reads its
/// config once when the daemon starts, this library re-resolves on every
/// construction, so a file appearing in an earlier directory afterwards splits
/// them until the daemon restarts. That would need something to place a
/// config file in an earlier-priority directory after start-up; this library
/// only ever reads the file, it never writes one.
///
/// The order is fixed by RAUC (src/context.c), not by this BSP, which is why it
/// is not overridable the way the app-image store and the scratch path are.
inline const std::vector<std::string> &rauc_config_search_paths()
{
    static const std::vector<std::string> paths = {
        std::string{updater::config::RAUC_SYSTEM_PATH},
        "/run/rauc/system.conf",
        "/usr/lib/rauc/system.conf",
    };
    return paths;
}

/// First candidate the predicate accepts, or an empty string when none does.
/// The predicate is a parameter rather than a direct path_exists() call so the
/// order can be tested without a filesystem.
template <typename Predicate>
std::string resolve_rauc_config(const std::vector<std::string> &candidates, Predicate exists)
{
    for (const std::string &candidate : candidates) {
        if (exists(candidate)) {
            return candidate;
        }
    }
    return std::string{};
}

/// Where the keyring named by a config file actually lives: an absolute path is
/// used as-is, a relative one resolves against the directory of the config that
/// was found -- not against a fixed /etc/rauc. RAUC resolves it the same way
/// (src/utils.c resolve_path), and that agreement is what keeps both sides on
/// one trust root when the config is not in the first candidate directory.
inline std::string resolve_keyring_path(const std::string &config_path,
                                        const std::string &keyring_path)
{
    const std::string dir = fs::util::parent_path(config_path);
    // parent_path() yields "" for a bare filename; the keyring is then relative
    // to the working directory, which is what "." says out loud.
    return fs::util::path_join(dir.empty() ? std::string{"."} : dir, keyring_path);
}

/// Human-readable list of the candidates, for the error a caller sees when none
/// of them exists. Naming only the first would send the reader to the wrong
/// directory.
inline std::string rauc_config_candidates_text()
{
    std::string text;
    for (const std::string &candidate : rauc_config_search_paths()) {
        if (!text.empty()) {
            text += ", ";
        }
        text += candidate;
    }
    return text;
}
}
