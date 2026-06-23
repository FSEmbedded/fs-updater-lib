#pragma once
// Lightweight expected<T,E>. For T=void actions, return fs::Error directly;
// expected is for value-returning actions (e.g. expected<version_t,
// ErrorInfo>). value() carries a has_value() precondition and NEVER throws,
// so it stays valid under -fno-exceptions.

#include <new>
#include <type_traits>
#include <utility>

namespace fs {

template <typename E>
class unexpected {
    E err_;
public:
    explicit unexpected(E e) : err_(std::move(e)) {}
    [[nodiscard]] const E& error() const & noexcept { return err_; }
    [[nodiscard]] E&& error() && noexcept { return std::move(err_); }
};

template <typename T, typename E>
class [[nodiscard]] expected {
    static_assert(!std::is_void_v<T>, "use fs::Error directly for void actions");
    bool has_;
    union {
        T val_;
        E err_;
    };

    void destroy() noexcept {
        if (has_) { val_.~T(); } else { err_.~E(); }
    }

public:
    using value_type = T;
    using error_type = E;

    expected(const T& v) : has_(true) { ::new (static_cast<void*>(&val_)) T(v); }
    expected(T&& v) : has_(true) { ::new (static_cast<void*>(&val_)) T(std::move(v)); }
    expected(unexpected<E> u) : has_(false) { ::new (static_cast<void*>(&err_)) E(std::move(u).error()); }

    expected(const expected& o) : has_(o.has_) {
        if (has_) { ::new (static_cast<void*>(&val_)) T(o.val_); }
        else      { ::new (static_cast<void*>(&err_)) E(o.err_); }
    }
    expected(expected&& o) noexcept(
        std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>)
        : has_(o.has_) {
        if (has_) { ::new (static_cast<void*>(&val_)) T(std::move(o.val_)); }
        else      { ::new (static_cast<void*>(&err_)) E(std::move(o.err_)); }
    }
    expected& operator=(const expected& o) {
        if (this != &o) { destroy(); has_ = o.has_;
            if (has_) { ::new (static_cast<void*>(&val_)) T(o.val_); }
            else      { ::new (static_cast<void*>(&err_)) E(o.err_); } }
        return *this;
    }
    ~expected() { destroy(); }

    [[nodiscard]] bool has_value() const noexcept { return has_; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_; }

    // value(): precondition has_value(); error(): precondition !has_value().
    [[nodiscard]] const T& value() const & noexcept { return val_; }
    [[nodiscard]] T& value() & noexcept { return val_; }
    [[nodiscard]] const E& error() const & noexcept { return err_; }
    [[nodiscard]] E& error() & noexcept { return err_; }

    template <typename U>
    [[nodiscard]] T value_or(U&& fallback) const& {
        return has_ ? val_ : static_cast<T>(std::forward<U>(fallback));
    }

    // map: T -> U, error propagated.
    template <typename F>
    [[nodiscard]] auto map(F&& f) const
        -> expected<std::decay_t<decltype(f(std::declval<const T&>()))>, E> {
        using U = std::decay_t<decltype(f(std::declval<const T&>()))>;
        if (has_) { return expected<U, E>(f(val_)); }
        return expected<U, E>(unexpected<E>(err_));
    }

    // and_then: T -> expected<U,E>, error propagated.
    template <typename F>
    [[nodiscard]] auto and_then(F&& f) const -> decltype(f(std::declval<const T&>())) {
        using R = decltype(f(std::declval<const T&>()));
        if (has_) { return f(val_); }
        return R(unexpected<E>(err_));
    }
};

} // namespace fs
