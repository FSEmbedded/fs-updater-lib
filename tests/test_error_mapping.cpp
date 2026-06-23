#include <gtest/gtest.h>

#include "handle_update/error_mapping.h"
#include "handle_update/fs_exceptions.h"
#include "subprocess/subprocess.h"

#include <cerrno>
#include <stdexcept>
#include <sys/types.h>

// Characterization gate for fs::classify_active_exception() — the lib-side half of the CLI
// exit-code contract (operation x category x errno; see the coding standard).
// Each thrown type maps to a fixed {category, errno}; the throw->return conversion
// (§3.2) must reproduce exactly this per converted site, so this test
// must stay GREEN across every conversion — it is the equivalence oracle.
//
// Coverage boundary (deliberate): the exception families defined in headers entangled with
// libubootenv / libsystemd — UBoot::UBootError*, rauc::RaucBaseException* (incl. the D-Bus
// client), updater::* (handleUpdate.h / updateFirmware.h) and the global applicationImage
// types — are NOT includable in the native test build. tests/CMakeLists.txt deliberately
// avoids libubootenv, and UBoot.h pulls <libuboot.h> while rauc_dbus_client.h pulls
// libsystemd. Their category is fixed by base class and verified by inheritance inspection
// (recorded in §2): every fs::BaseFSUpdateException-derived type ->
// `internal` (the same ladder branch exercised by ApplicationVersion below); every type
// rooted at a std::exception-derived family base (UBootError / RaucBaseException /
// SubprocessError) -> `system` (the same catch-all branch exercised by subprocess below).
// So every classification *mechanism* is exercised here; only direct per-type instantiation
// of the heavy families is out of reach. Making them directly characterizable means
// extracting lightweight, dependency-free exception headers — a §3.1
// firewall change, tracked there as an open item.

namespace {
template <typename Thrower>
fs::ErrorInfo classify_thrown(Thrower&& t) {
    try {
        t();
    } catch (...) {
        return fs::classify_active_exception();
    }
    ADD_FAILURE() << "thrower did not throw";
    return fs::ErrorInfo{};
}
} // namespace

TEST(ExceptionClassify, UpdateInProgress) {
    const auto info = classify_thrown([] { throw fs::UpdateInProgress("x"); });
    EXPECT_EQ(info.code, fs::Error::update_in_progress);
    EXPECT_EQ(info.errno_val, 0);
}

TEST(ExceptionClassify, GenericWithoutErrno) {
    const auto info = classify_thrown([] { throw fs::GenericException("x"); });
    EXPECT_EQ(info.code, fs::Error::generic);
    EXPECT_EQ(info.errno_val, 0);
}

TEST(ExceptionClassify, GenericCarriesErrno) {
    // The exit-54 carry: switch_*_slot branches on EPERM/ECANCELED, so the errno must
    // survive the mapping unchanged.
    const auto eperm = classify_thrown([] { throw fs::GenericException("x", EPERM); });
    EXPECT_EQ(eperm.code, fs::Error::generic);
    EXPECT_EQ(eperm.errno_val, EPERM);

    const auto ecanceled = classify_thrown([] { throw fs::GenericException("x", ECANCELED); });
    EXPECT_EQ(ecanceled.code, fs::Error::generic);
    EXPECT_EQ(ecanceled.errno_val, ECANCELED);
}

TEST(ExceptionClassify, GenericSubclassesStayGenericWithSeededErrno) {
    // Inheritance dispatch: a GenericException subclass still lands in `generic`, carrying
    // the errno its constructor seeds.
    const auto unknown = classify_thrown([] { throw fs::UnknownUpdateFormat("x"); });
    EXPECT_EQ(unknown.code, fs::Error::generic);
    EXPECT_EQ(unknown.errno_val, ENOTSUP);

    const auto unsupported = classify_thrown([] { throw fs::UpdateFormatNotSupported("x"); });
    EXPECT_EQ(unsupported.code, fs::Error::generic);
    EXPECT_EQ(unsupported.errno_val, ENOSYS);
}

TEST(ExceptionClassify, NotAllowedUpdateStateBeatsTheBaseCatch) {
    // NotAllowedUpdateState derives from BaseFSUpdateException but its catch clause is
    // ordered before the base clause, so it must reach `not_allowed_state`, not `internal`.
    const auto info = classify_thrown([] { throw fs::NotAllowedUpdateState(); });
    EXPECT_EQ(info.code, fs::Error::not_allowed_state);
    EXPECT_EQ(info.errno_val, 0);
}

TEST(ExceptionClassify, BaseDerivedValueErrorsAreInternal) {
    // Representatives of the ~30 BaseFSUpdateException-derived "value/state" types (the rest
    // live in libuboot-entangled headers; same ladder branch, see the coverage note above).
    for (const auto& info : {
             classify_thrown([] { throw fs::ApplicationVersion(2, 1); }),
             classify_thrown([] { throw fs::FirmwareVersion(2, 1); }),
             classify_thrown([] { throw fs::FirmwareApplicationVersion(2, 1, 2, 1); }),
         }) {
        EXPECT_EQ(info.code, fs::Error::internal);
        EXPECT_EQ(info.errno_val, 0);
    }
}

TEST(ExceptionClassify, SubprocessFamilyIsSystem) {
    // subprocess::SubprocessError is rooted at std::exception (not BaseFSUpdateException), so
    // it falls to the catch-all `system` — the same branch that classifies every
    // UBoot::UBootError and rauc::RaucBaseException type.
    const auto child = classify_thrown([] { throw subprocess::ChildProcess(pid_t{0}, "x"); });
    EXPECT_EQ(child.code, fs::Error::system);
    EXPECT_EQ(child.errno_val, 0);

    const auto read_pipe = classify_thrown([] { throw subprocess::ReadPipe(EIO); });
    EXPECT_EQ(read_pipe.code, fs::Error::system);
    EXPECT_EQ(read_pipe.errno_val, 0);
}

TEST(ExceptionClassify, ForeignStdExceptionsAreSystem) {
    EXPECT_EQ(classify_thrown([] { throw std::runtime_error("x"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw std::logic_error("x"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw std::overflow_error("x"); }).code, fs::Error::system);
}
