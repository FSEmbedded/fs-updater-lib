#pragma once
// Project-wide error taxonomy, seeded from the CLI's five catch categories
// (the binding exit-code contract), NOT from the exception class list — so
// the (operation x category x errno) -> exit-code mapping stays expressible
// independent of whether a call site throws or returns an error.
//
//   CLI catch                       -> category          -> exit bucket
//   fs::UpdateInProgress            -> update_in_progress    PROGRESS_ERROR
//   fs::GenericException (w/ errno)  -> generic               PROGRESS_ERROR
//   fs::NotAllowedUpdateState        -> not_allowed_state     NOT_ALLOWED_UBOOT_STATE (exit 54)
//   fs::BaseFSUpdateException        -> internal              INTERNAL_ERROR
//   std::exception (foreign)         -> system                SYSTEM_ERROR
//
// errno_val carries GenericException::errorno: switch_*_slot branches on EPERM/
// ECANCELED to reach exit 54, so the category alone is insufficient — the errno
// must ride along from the first definition (retrofitting it would re-touch every
// converted site).

#include <cstdint>
#include <string_view>

namespace fs {

// Append-only, explicit values — ABI-stable. New categories go at the end.
enum class Error : std::uint8_t {
    none = 0,
    update_in_progress = 1,
    generic = 2,
    not_allowed_state = 3,
    internal = 4,
    system = 5,
};

// The category plus the errno that produced it (0 when not errno-derived).
struct ErrorInfo {
    Error code = Error::none;
    int errno_val = 0;

    [[nodiscard]] constexpr bool ok() const noexcept { return code == Error::none; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok(); }
};

[[nodiscard]] constexpr std::string_view describe(Error e) noexcept {
    switch (e) {
        case Error::none:               return "none";
        case Error::update_in_progress: return "update_in_progress";
        case Error::generic:            return "generic";
        case Error::not_allowed_state:  return "not_allowed_state";
        case Error::internal:           return "internal";
        case Error::system:             return "system";
    }
    return "unknown";
}

} // namespace fs
