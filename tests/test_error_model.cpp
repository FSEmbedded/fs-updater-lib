#include <gtest/gtest.h>

#include "error.h"
#include "expected.h"

#include <cerrno>
#include <string>

using fs::Error;
using fs::ErrorInfo;
using fs::expected;
using fs::unexpected;

TEST(ErrorInfo, OkAndDescribe) {
    constexpr ErrorInfo good{};
    EXPECT_TRUE(good.ok());
    EXPECT_TRUE(static_cast<bool>(good));
    EXPECT_EQ(good.code, Error::none);

    constexpr ErrorInfo bad{Error::generic, EPERM};
    EXPECT_FALSE(bad.ok());
    EXPECT_EQ(bad.errno_val, EPERM);
    EXPECT_EQ(fs::describe(Error::not_allowed_state), "not_allowed_state");
    EXPECT_EQ(fs::describe(Error::system), "system");
}

TEST(Expected, ValuePath) {
    expected<int, ErrorInfo> e = 42;
    ASSERT_TRUE(e.has_value());
    EXPECT_TRUE(static_cast<bool>(e));
    EXPECT_EQ(e.value(), 42);
    EXPECT_EQ(e.value_or(-1), 42);
}

TEST(Expected, ErrorPath) {
    expected<int, ErrorInfo> e = unexpected<ErrorInfo>({Error::not_allowed_state, ECANCELED});
    ASSERT_FALSE(e.has_value());
    EXPECT_EQ(e.error().code, Error::not_allowed_state);
    EXPECT_EQ(e.error().errno_val, ECANCELED);   // the exit-54 carry
    EXPECT_EQ(e.value_or(-1), -1);
}

TEST(Expected, MapPropagatesValueAndError) {
    expected<int, ErrorInfo> ok = 21;
    auto doubled = ok.map([](int v) { return v * 2; });
    ASSERT_TRUE(doubled.has_value());
    EXPECT_EQ(doubled.value(), 42);

    expected<int, ErrorInfo> err = unexpected<ErrorInfo>({Error::system, EIO});
    auto mapped = err.map([](int v) { return v * 2; });
    ASSERT_FALSE(mapped.has_value());
    EXPECT_EQ(mapped.error().errno_val, EIO);
}

TEST(Expected, AndThenChains) {
    auto half = [](int v) -> expected<int, ErrorInfo> {
        if (v % 2 != 0) { return unexpected<ErrorInfo>({Error::generic, EINVAL}); }
        return v / 2;
    };
    expected<int, ErrorInfo> ok = 8;
    auto r = ok.and_then(half);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 4);

    expected<int, ErrorInfo> odd = 7;
    auto r2 = odd.and_then(half);
    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code, Error::generic);
}

TEST(Expected, NonTrivialValueType) {
    expected<std::string, ErrorInfo> e = std::string("hello");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e.value(), "hello");
    auto len = e.map([](const std::string& s) { return s.size(); });
    ASSERT_TRUE(len.has_value());
    EXPECT_EQ(len.value(), 5u);

    // copy + move
    expected<std::string, ErrorInfo> copy = e;
    EXPECT_EQ(copy.value(), "hello");
    expected<std::string, ErrorInfo> moved = std::move(copy);
    EXPECT_EQ(moved.value(), "hello");
}
