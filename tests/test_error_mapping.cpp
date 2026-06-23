#include <gtest/gtest.h>

#include "handle_update/error_mapping.h"
#include "handle_update/fs_exceptions.h"

#include <cerrno>
#include <stdexcept>

namespace {
template <typename Thrower>
fs::ErrorInfo classify_thrown(Thrower&& t) {
    try { t(); } catch (...) { return fs::classify_active_exception(); }
    ADD_FAILURE() << "thrower did not throw";
    return fs::ErrorInfo{};
}
} // namespace

TEST(ExceptionClassify, MapsEachCategory) {
    EXPECT_EQ(classify_thrown([] { throw fs::UpdateInProgress("x"); }).code,
              fs::Error::update_in_progress);

    const auto g = classify_thrown([] { throw fs::GenericException("x", EPERM); });
    EXPECT_EQ(g.code, fs::Error::generic);
    EXPECT_EQ(g.errno_val, EPERM);   // the exit-54 carry survives the mapping

    EXPECT_EQ(classify_thrown([] { throw fs::NotAllowedUpdateState(); }).code,
              fs::Error::not_allowed_state);

    // a value-error subclass of the base maps to internal
    EXPECT_EQ(classify_thrown([] { throw fs::ApplicationVersion(2, 1); }).code,
              fs::Error::internal);

    // a GenericException subclass still maps to generic (inheritance dispatch)
    EXPECT_EQ(classify_thrown([] { throw fs::UnknownUpdateFormat("x"); }).code,
              fs::Error::generic);

    // a foreign std::exception maps to system (the firewall catch-all)
    EXPECT_EQ(classify_thrown([] { throw std::runtime_error("x"); }).code,
              fs::Error::system);
}
