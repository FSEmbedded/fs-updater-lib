#pragma once

// RAII guards and POSIX path helpers, sharing the primitives the
// dynamic-overlay component already uses. All operations are noexcept and
// report failure via the return value + errno (never by throwing), so they
// stay valid once the library is built without exceptions. These let call
// sites drop <filesystem> during the filesystem→POSIX migration.

extern "C" {
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
}

#include <cerrno>
#include <cstdint>
#include <optional>
#include <string>

// ::rename is declared only in <stdio.h>/<cstdio>, which the project bans;
// forward-declare it so rename_file can call it without pulling that header.
extern "C" int rename(const char *from, const char *to) noexcept;

namespace fs::util {

// RAII wrapper for DIR* handles — closes on destruction.
class DirGuard {
    DIR *dir_ = nullptr;

public:
    explicit DirGuard(DIR *d) noexcept : dir_(d) {}
    ~DirGuard() noexcept
    {
        if (dir_ != nullptr) {
            ::closedir(dir_);
        }
    }

    DirGuard(const DirGuard &) = delete;
    DirGuard &operator=(const DirGuard &) = delete;
    DirGuard(DirGuard &&) = delete;
    DirGuard &operator=(DirGuard &&) = delete;

    [[nodiscard]] DIR *get() const noexcept { return dir_; }
    [[nodiscard]] bool valid() const noexcept { return dir_ != nullptr; }
};

// RAII wrapper for file descriptors — closes on destruction.
class FdGuard {
    int fd_{-1};

public:
    FdGuard() noexcept = default;
    explicit FdGuard(int fd) noexcept : fd_{fd} {}

    ~FdGuard() noexcept
    {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;

    FdGuard(FdGuard &&other) noexcept : fd_{other.fd_}
    {
        other.fd_ = -1;
    }

    FdGuard &operator=(FdGuard &&other) noexcept
    {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    int release() noexcept
    {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }
};

// RAII scope guard — runs a cleanup action on destruction unless dismissed.
template <typename Func>
class ScopeGuard {
    Func fn_;
    bool active_{true};

public:
    explicit ScopeGuard(Func fn) noexcept : fn_(fn) {}
    ~ScopeGuard() noexcept
    {
        if (active_) {
            fn_();
        }
    }

    void dismiss() noexcept { active_ = false; }

    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator=(const ScopeGuard &) = delete;
    ScopeGuard(ScopeGuard &&) = delete;
    ScopeGuard &operator=(ScopeGuard &&) = delete;
};

// --- POSIX path helpers ----------------------------------------------------
// Each mirrors the std::filesystem operation the library currently uses, but
// stays in <sys/stat.h>/<unistd.h> (no banned <filesystem>/<cstdio>) and
// reports failure via the return value + errno. (rename is intentionally not
// here yet — it is declared only in the banned <stdio.h>; it joins when its
// one call site is migrated.)

// True if `path` exists (any file type).
[[nodiscard]] inline bool path_exists(const std::string &path) noexcept
{
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// True if `path` exists and is a directory.
[[nodiscard]] inline bool is_directory(const std::string &path) noexcept
{
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Recursively create `path` and any missing parents (like `mkdir -p`).
// True on success or if it already exists; false with errno set otherwise.
[[nodiscard]] inline bool mkdir_p(const std::string &path, mode_t mode = 0755) noexcept
{
    if (path.empty()) {
        errno = EINVAL;
        return false;
    }
    for (std::string::size_type pos = path.find('/', 1); pos != std::string::npos;
         pos = path.find('/', pos + 1)) {
        const std::string parent = path.substr(0, pos);
        if (::mkdir(parent.c_str(), mode) != 0 && errno != EEXIST) {
            return false;
        }
    }
    return ::mkdir(path.c_str(), mode) == 0 || errno == EEXIST;
}

// Remove a file or empty directory. True if something was removed; false if it
// did not exist or on error (errno set). Mirrors std::filesystem::remove.
[[nodiscard]] inline bool remove_file(const std::string &path) noexcept
{
    if (::unlink(path.c_str()) == 0) {
        return true;
    }
    if (errno == EISDIR && ::rmdir(path.c_str()) == 0) {
        return true;
    }
    return false;
}

// Rename/move `from` to `to` (atomic within a filesystem). True on success,
// false with errno set. Mirrors std::filesystem::rename's effect.
[[nodiscard]] inline bool rename_file(const std::string &from, const std::string &to) noexcept
{
    return ::rename(from.c_str(), to.c_str()) == 0;
}

// Size of the file at `path` in bytes, or nullopt on error.
[[nodiscard]] inline std::optional<std::uintmax_t> file_size(const std::string &path) noexcept
{
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) {
        return std::nullopt;
    }
    return static_cast<std::uintmax_t>(st.st_size);
}

// chmod `path` to `mode`. True on success, false with errno set.
[[nodiscard]] inline bool set_permissions(const std::string &path, mode_t mode) noexcept
{
    return ::chmod(path.c_str(), mode) == 0;
}

// The parent directory of `path` (everything before the last '/'). Returns "/"
// for a root child and "" when there is no '/'. Pure string op; no I/O.
[[nodiscard]] inline std::string parent_path(const std::string &path)
{
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return std::string{};
    }
    if (pos == 0) {
        return std::string{"/"};
    }
    return path.substr(0, pos);
}

// Join `base` and `leaf`, mirroring std::filesystem::operator/ for the cases in
// use: an empty base yields the leaf; an absolute leaf (leading '/') replaces
// the base; otherwise exactly one '/' separates them.
[[nodiscard]] inline std::string path_join(const std::string &base, const std::string &leaf)
{
    if (base.empty()) {
        return leaf;
    }
    if (!leaf.empty() && leaf.front() == '/') {
        return leaf;
    }
    if (base.back() == '/') {
        return base + leaf;
    }
    return base + '/' + leaf;
}

} // namespace fs::util
