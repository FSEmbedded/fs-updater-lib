# Prefer system gtest (Yocto sysroot, distro package) so disconnected
# builds (FETCHCONTENT_FULLY_DISCONNECTED=ON under bitbake) work without
# network access. Fall back to FetchContent for host development.
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    message(STATUS "System GTest not found — falling back to FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

set(TEST_COMPILE_OPTIONS -Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
if(FUS_LEGACY_IMAGE_SUPPORT)
    pkg_check_modules(BOTAN2 REQUIRED IMPORTED_TARGET botan-2)
endif()

set(TEST_SOURCES
    test_posix_guards.cpp
    test_update_bits_write.cpp
    test_error_model.cpp
    test_error_mapping.cpp
    test_rauc_failure_texts.cpp
    test_logger.cpp
    test_detect_update_format.cpp
    test_classify_dispatch.cpp
    test_make_update_source.cpp
    test_rauc_bundle_source.cpp
    test_base_exception.cpp
    test_update_store_v2.cpp
    test_bounded_reader.cpp
    test_update_stream_sink.cpp
    test_rauc_install_sink.cpp
    test_app_bundle_install.cpp
    test_rauc_config_path.cpp
    test_resolve_scratch_dir.cpp
    test_inspect_bundle.cpp
    test_progress_remap.cpp
    test_progress_interpolation.cpp
    test_wait_loss.cpp
    test_bootstate_state_machine.cpp
    test_reboot_state_totality.cpp
    test_application_update.cpp
)

# Cases for the units in LIB_LEGACY_IMAGE_SOURCES; they leave with them.
if(FUS_LEGACY_IMAGE_SUPPORT)
    list(APPEND TEST_SOURCES
        test_update_container_source.cpp
        test_application_image_source.cpp
        test_descriptor.cpp
        test_sha256_hasher.cpp
        test_extract_member.cpp
        test_open_v2_container.cpp
        test_v2_container_reader.cpp
        test_application_image.cpp
        test_cert_image_verifier.cpp
    )
endif()

if(BUILD_DBUS_SUPPORT)
    list(APPEND TEST_SOURCES
        test_sd_bus_connection.cpp
        test_sd_bus_match.cpp
        test_inspect_reply.cpp
    )
endif()

add_executable(fs_updater_lib_tests ${TEST_SOURCES})

target_compile_features(fs_updater_lib_tests PRIVATE cxx_std_17)
set_target_properties(fs_updater_lib_tests PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(fs_updater_lib_tests PRIVATE ${TEST_COMPILE_OPTIONS})

target_include_directories(fs_updater_lib_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)

# The write-path guard scans the shipped sources for a call site that writes
# the update bitfield without going through the validating helper.
target_compile_definitions(fs_updater_lib_tests PRIVATE
    FS_UPDATER_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
)

# updateApplication.cpp (LIB_TESTABLE_SOURCES below) uses boost::property_tree
# for its RAUC INI config parse. Boost is header-only here but not installed
# on a bare host; every documented build.sh invocation already sets SDK_ROOT,
# so pick its headers up from there automatically instead of adding a new
# required flag to this script.
# Symlink only the boost/ subtree (not the SDK's whole usr/include) into the
# build dir: the full sysroot include also carries glibc's own bits/features
# headers, which shadow the host's and break every other native TU (target
# aarch64 glibc headers pulled into an x86_64 g++ compile).
if(DEFINED ENV{SDK_ROOT})
    set(SDK_BOOST_SRC "$ENV{SDK_ROOT}/sysroots/cortexa53-fslc-linux/usr/include/boost")
    if(EXISTS "${SDK_BOOST_SRC}")
        set(BOOST_INCLUDE_STAGE "${CMAKE_CURRENT_BINARY_DIR}/boost-include")
        file(MAKE_DIRECTORY "${BOOST_INCLUDE_STAGE}")
        if(NOT EXISTS "${BOOST_INCLUDE_STAGE}/boost")
            file(CREATE_LINK "${SDK_BOOST_SRC}" "${BOOST_INCLUDE_STAGE}/boost" SYMBOLIC)
        endif()
        target_include_directories(fs_updater_lib_tests SYSTEM PRIVATE ${BOOST_INCLUDE_STAGE})
    endif()
endif()

# Testable source files: LIB_TESTABLE_SOURCES is defined once in the parent
# CMakeLists.txt (native "tests-only" section) and shared with fuzz/ — both
# the gtest binary and the fuzz targets link the exact same list, so there's
# one place that defines "what fs-updater-lib code builds without
# libubootenv/libarchive". Avoids pulling those in transitively from
# fs_update_static.
target_sources(fs_updater_lib_tests PRIVATE ${LIB_TESTABLE_SOURCES})

find_package(ZLIB REQUIRED)
target_link_libraries(fs_updater_lib_tests PRIVATE PkgConfig::JSONCPP ZLIB::ZLIB)
if(FUS_LEGACY_IMAGE_SUPPORT)
    target_link_libraries(fs_updater_lib_tests PRIVATE PkgConfig::BOTAN2)
endif()

if(BUILD_DBUS_SUPPORT)
    # Hosts without native libsystemd-dev can point these at vendored
    # headers + a .so shim (same override contract as fs-updater-service):
    #   -DLIBSYSTEMD_INCLUDE=<inc_dir> -DLIBSYSTEMD_LIB=<lib_dir>
    if(DEFINED LIBSYSTEMD_INCLUDE AND DEFINED LIBSYSTEMD_LIB)
        target_include_directories(fs_updater_lib_tests SYSTEM PRIVATE ${LIBSYSTEMD_INCLUDE})
        target_link_directories(fs_updater_lib_tests PRIVATE ${LIBSYSTEMD_LIB})
        target_link_libraries(fs_updater_lib_tests PRIVATE systemd)
    else()
        pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
        target_link_libraries(fs_updater_lib_tests PRIVATE PkgConfig::SYSTEMD)
    endif()
    target_sources(fs_updater_lib_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/dbus/sd_bus_connection.cpp
        ${CMAKE_SOURCE_DIR}/src/dbus/sd_bus_match.cpp
        ${CMAKE_SOURCE_DIR}/src/dbus/inspect_reply.cpp
        # The outer update object and the two collaborators it constructs.
        # Not in LIB_TESTABLE_SOURCES: that list is shared with fuzz/, which
        # deliberately never links libsystemd, and the RAUC client needs it.
        # So the outer object is drivable natively only in a D-Bus build.
        ${CMAKE_SOURCE_DIR}/src/dbus/rauc_dbus_client.cpp
        ${CMAKE_SOURCE_DIR}/src/handle_update/updateFirmware.cpp
        ${CMAKE_SOURCE_DIR}/src/handle_update/fsupdate.cpp
        ${CMAKE_SOURCE_DIR}/src/handle_update/RaucApplicationUpdate.cpp
    )
    # Drives the outer object through the injected environment; belongs with
    # the sources above because it cannot build without them.
    target_sources(fs_updater_lib_tests PRIVATE
        test_fsupdate_seam.cpp
    )
endif()

target_link_libraries(fs_updater_lib_tests PRIVATE GTest::gtest_main)

add_test(NAME fs_updater_lib_tests COMMAND fs_updater_lib_tests)
# Per-case XML: ctest's own JUnit report has one entry for this whole
# binary, because ctest registers the binary and not the cases.
set_tests_properties(fs_updater_lib_tests PROPERTIES
    ENVIRONMENT "GTEST_OUTPUT=xml:${CMAKE_BINARY_DIR}/gtest-fs_updater_lib_tests.xml")
