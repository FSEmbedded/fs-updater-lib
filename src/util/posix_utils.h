#pragma once

// RAII guards and POSIX path helpers, sharing the primitives the
// dynamic-overlay component already uses. The path helpers are all noexcept
// and report failure via the return value + errno (never by throwing), so
// they stay valid under -fno-exceptions and avoid <filesystem>'s exceptions.
// Definitions of the free functions live in posix_utils.cpp; the guards are
// inline below.

extern "C" {
#include <dirent.h>
#include <sys/types.h> // mode_t
#include <unistd.h>
}

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

// --- POSIX path helpers (defined in posix_utils.cpp) -----------------------
// Each mirrors the std::filesystem operation the library used to call, but
// stays in <sys/stat.h>/<unistd.h>/<fcntl.h> (no banned <filesystem>/<cstdio>)
// and reports failure via the return value + errno. Parameters are
// std::string_view (non-owning) per the coding standard; the definitions
// construct a std::string internally only where a null-terminated c_str() is
// required for the syscall.

// True if `path` exists (any file type).
[[nodiscard]] bool path_exists(std::string_view path) noexcept;

// True if `path` exists and is a directory.
[[nodiscard]] bool is_directory(std::string_view path) noexcept;

// Recursively create `path` and any missing parents (like `mkdir -p`).
// True on success or if it already exists; false with errno set otherwise.
[[nodiscard]] bool mkdir_p(std::string_view path, mode_t mode = 0755) noexcept;

// Remove a file or empty directory. True if something was removed; false if it
// did not exist or on error (errno set). Mirrors std::filesystem::remove.
[[nodiscard]] bool remove_file(std::string_view path) noexcept;

// Rename/move `from` to `to` (atomic within a filesystem). True on success,
// false with errno set. Mirrors std::filesystem::rename's effect.
[[nodiscard]] bool rename_file(std::string_view from, std::string_view to) noexcept;

// Size of the file at `path` in bytes, or nullopt on error.
[[nodiscard]] std::optional<std::uintmax_t> file_size(std::string_view path) noexcept;

// chmod `path` to `mode`. True on success, false with errno set.
[[nodiscard]] bool set_permissions(std::string_view path, mode_t mode) noexcept;

// The parent directory of `path` (everything before the last '/'). Returns "/"
// for a root child and "" when there is no '/'. Pure string op; no I/O.
[[nodiscard]] std::string parent_path(std::string_view path);

// Join `base` and `leaf`, mirroring std::filesystem::operator/ for the cases in
// use: an empty base yields the leaf; an absolute leaf (leading '/') replaces
// the base; otherwise exactly one '/' separates them.
[[nodiscard]] std::string path_join(std::string_view base, std::string_view leaf);

} // namespace fs::util
