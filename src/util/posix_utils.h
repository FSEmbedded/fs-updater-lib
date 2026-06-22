#pragma once

// RAII guards for POSIX resources, sharing the same primitives the
// dynamic-overlay component already uses. All operations are noexcept, so the
// guards stay valid once the library is built without exceptions. POSIX path
// helpers (mkdir_p, read/write file, ...) are added here alongside the
// filesystem migration.

extern "C" {
#include <dirent.h>
#include <unistd.h>
}

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

} // namespace fs::util
