#include <gtest/gtest.h>

#include "handle_update/error_mapping.h"
#include "handle_update/fs_exceptions.h"
#include "uboot_interface/uboot_exceptions.h"
#include "rauc/rauc_exceptions.h"
#include "handle_update/updater_exceptions.h"
#include "handle_update/applicationimage_exceptions.h"

#include <cerrno>
#include <stdexcept>
#include <sys/types.h>

// Pins fs::classify_active_exception() — the lib-side half of the CLI
// exit-code contract (operation x category x errno). Each thrown type maps
// to a fixed {category, errno}; this test is the equivalence oracle any
// throw<->return conversion at a call site must reproduce exactly.
//
// Every concrete thrown exception family is characterized directly,
// including UBoot::, rauc::, updater::*, and the global applicationImage
// types, whose definitions live in dependency-free headers
// (uboot_exceptions.h, rauc_exceptions.h, updater_exceptions.h,
// applicationimage_exceptions.h) so this test includes them without pulling
// <libuboot.h> / libsystemd / json / the engine headers.
// fs::ApplyUpdateInvalidState lives in fs_exceptions.h. Classification is
// exercised per concrete type, not by representative + inheritance
// inspection.

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

/* A Commit against a pending-but-unbooted firmware update is exactly the
 * "wrong moment" shape NotAllowedUpdateState exists for: MissingReboot must
 * map to not_allowed_state, not internal. */
TEST(ExceptionClassify, MissingRebootIsNotAllowedState) {
    const auto info = classify_thrown([] { throw updater::MissingReboot("x"); });
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

TEST(ExceptionClassify, UpdaterFamilyIsInternal) {
    // updater:: types derive from fs::BaseFSUpdateException -> internal. Now directly testable via
    // the extracted updater_exceptions.h. Sampled across both source headers, incl. the two
    // Rollback* types the engine catches by subtype (they must still collapse to internal).
    for (const auto& info : {
             classify_thrown([] { throw updater::GetLoopDevices("x"); }),
             classify_thrown([] { throw updater::ConfirmPendingFirmwareUpdate("x"); }),
             classify_thrown([] { throw updater::FirmwareRebootStateNotDefined(); }),
             classify_thrown([] { throw updater::RollbackFirmwareUpdate("x"); }),
             classify_thrown([] { throw updater::RollbackApplicationUpdate("x"); }),
             classify_thrown([] { throw updater::ReadCmdline("x"); }),
             classify_thrown([] { throw updater::FirmwareUpdateInstall("x"); }),
             classify_thrown([] { throw updater::GetFirmwareVersion("p", "x"); }),
             classify_thrown([] { throw updater::WrongVariableContent("v"); }),
             classify_thrown([] { throw updater::RaucDetection(); }),
         }) {
        EXPECT_EQ(info.code, fs::Error::internal);
        EXPECT_EQ(info.errno_val, 0);
    }
}

TEST(ExceptionClassify, ApplicationImageFamilyIsInternal) {
    // The global applicationImage types derive from fs::BaseFSUpdateException -> internal. Now
    // directly testable via the extracted applicationimage_exceptions.h.
    for (const auto& info : {
             classify_thrown([] { throw ReadPointOfTime("t"); }),
             classify_thrown([] { throw WrongHeaderVersion(2U); }),
             classify_thrown([] { throw OpenApplicationImage("p", "e"); }),
             classify_thrown([] { throw ImageUpdatePackageToSmall(); }),
             classify_thrown([] { throw WrongHeaderChecksum(1U, 2U); }),
             classify_thrown([] { throw DuringWriteApplicationImage("x"); }),
         }) {
        EXPECT_EQ(info.code, fs::Error::internal);
        EXPECT_EQ(info.errno_val, 0);
    }
}

/* Was `internal` until a consumer needed the distinction: "nothing to apply"
 * is the answer "no work pending", not a failure of the update path, and a
 * caller acts on the two differently. Over an interface that carries only the
 * category they were indistinguishable. */
TEST(ExceptionClassify, ApplyUpdateInvalidStateIsItsOwnCategory) {
    const auto info = classify_thrown([] { throw fs::ApplyUpdateInvalidState(2U); });
    EXPECT_EQ(info.code, fs::Error::nothing_to_apply);
    EXPECT_EQ(info.errno_val, 0);
}

TEST(ExceptionClassify, UBootFamilyIsSystem) {
    // UBoot::UBootError is std::exception-rooted (not BaseFSUpdateException) -> system. Now
    // directly testable via the extracted uboot_exceptions.h (no <libuboot.h> pulled).
    EXPECT_EQ(classify_thrown([] { throw UBoot::UBootEnvAccess("v"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw UBoot::UBootEnvWrite("v", "c"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw UBoot::UBootEnv("x"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw UBoot::UBootEnvVarNotAllowedContent("v", "c", "a"); }).code,
              fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw UBoot::UBootEnvVarCanNotConvertedIntoReturnType("v", "c"); }).code,
              fs::Error::system);
}

TEST(ExceptionClassify, RaucFamilyIsSystem) {
    // rauc::RaucBaseException is std::exception-rooted -> system. Now directly testable via the
    // extracted rauc_exceptions.h (no subprocess / libsystemd pulled). All 9 incl. the D-Bus pair.
    EXPECT_EQ(classify_thrown([] { throw rauc::ParseJson("x"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw rauc::MarkUBootEnv("x", true); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw rauc::RaucInstallBundle("b", "r"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw rauc::RaucGetArtifactInformation("b", "r"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw rauc::RaucMarkOtherPartition("r"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw rauc::RaucRollback("r"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw rauc::RaucGetStatus("r"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw rauc::RaucMarkGood("r"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw rauc::RaucServiceUnavailable("r"); }).code, fs::Error::system);
}

TEST(ExceptionClassify, ForeignStdExceptionsAreSystem) {
    EXPECT_EQ(classify_thrown([] { throw std::runtime_error("x"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw std::logic_error("x"); }).code, fs::Error::system);
    EXPECT_EQ(classify_thrown([] { throw std::overflow_error("x"); }).code, fs::Error::system);
}

/* And it must not have been taken from its siblings: the neighbouring
 * categories keep their answers, so this arm shadows none of them. */
TEST(ErrorMapping, TheNewCategoryDidNotShadowItsNeighbours)
{
    try {
        throw fs::NotAllowedUpdateState("still its own");
    } catch (...) {
        EXPECT_EQ(fs::classify_active_exception().code, fs::Error::not_allowed_state);
    }
    try {
        throw fs::UpdateInProgress("still its own");
    } catch (...) {
        EXPECT_EQ(fs::classify_active_exception().code, fs::Error::update_in_progress);
    }
}
