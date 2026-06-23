#include "posix_utils.h"

#include <cerrno>
#include <string>

extern "C" {
#include <fcntl.h> // ::rename — avoids the banned <stdio.h>/<cstdio>
#include <sys/stat.h>
#include <unistd.h>
}

namespace fs::util {

bool path_exists(std::string_view path) noexcept
{
    struct stat st{};
    const std::string p(path);
    return ::stat(p.c_str(), &st) == 0;
}

bool is_directory(std::string_view path) noexcept
{
    struct stat st{};
    const std::string p(path);
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool mkdir_p(std::string_view path, mode_t mode) noexcept
{
    if (path.empty()) {
        errno = EINVAL;
        return false;
    }
    const std::string full(path);
    for (std::string::size_type pos = full.find('/', 1); pos != std::string::npos;
         pos = full.find('/', pos + 1)) {
        const std::string parent = full.substr(0, pos);
        if (::mkdir(parent.c_str(), mode) != 0 && errno != EEXIST) {
            return false;
        }
    }
    return ::mkdir(full.c_str(), mode) == 0 || errno == EEXIST;
}

bool remove_file(std::string_view path) noexcept
{
    const std::string p(path);
    if (::unlink(p.c_str()) == 0) {
        return true;
    }
    if (errno == EISDIR && ::rmdir(p.c_str()) == 0) {
        return true;
    }
    return false;
}

bool rename_file(std::string_view from, std::string_view to) noexcept
{
    const std::string f(from);
    const std::string t(to);
    return ::rename(f.c_str(), t.c_str()) == 0;
}

std::optional<std::uintmax_t> file_size(std::string_view path) noexcept
{
    struct stat st{};
    const std::string p(path);
    if (::stat(p.c_str(), &st) != 0) {
        return std::nullopt;
    }
    return static_cast<std::uintmax_t>(st.st_size);
}

bool set_permissions(std::string_view path, mode_t mode) noexcept
{
    const std::string p(path);
    return ::chmod(p.c_str(), mode) == 0;
}

std::string parent_path(std::string_view path)
{
    const std::string_view::size_type pos = path.find_last_of('/');
    if (pos == std::string_view::npos) {
        return std::string{};
    }
    if (pos == 0) {
        return std::string{"/"};
    }
    return std::string{path.substr(0, pos)};
}

std::string path_join(std::string_view base, std::string_view leaf)
{
    if (base.empty()) {
        return std::string{leaf};
    }
    if (!leaf.empty() && leaf.front() == '/') {
        return std::string{leaf};
    }
    std::string result{base};
    if (base.back() != '/') {
        result += '/';
    }
    result.append(leaf);
    return result;
}

} // namespace fs::util
