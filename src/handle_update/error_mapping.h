#pragma once
// Bridge from the legacy exception hierarchy to the new ErrorInfo model.
//
// classify_active_exception() inspects the currently in-flight exception and
// returns its ErrorInfo category. It MUST be called from within a catch block:
// it re-raises the active exception and dispatches on its dynamic type via a
// catch ladder, so it needs no RTTI (dynamic_cast is banned).

#include "../error.h"

namespace fs {

[[nodiscard]] ErrorInfo classify_active_exception() noexcept;

} // namespace fs
