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
pkg_check_modules(BOTAN2 REQUIRED IMPORTED_TARGET botan-2)

set(TEST_SOURCES
    test_logger.cpp
    test_subprocess.cpp
    test_base_exception.cpp
    test_update_store_v2.cpp
    test_bounded_reader.cpp
    test_update_stream_sink.cpp
    test_descriptor.cpp
    test_sha256_hasher.cpp
    test_extract_member.cpp
    test_open_v2_container.cpp
    test_v2_container_reader.cpp
    test_rauc_install_sink.cpp
    test_resolve_scratch_dir.cpp
    test_inspect_bundle.cpp
    test_progress_remap.cpp
    test_progress_interpolation.cpp
)

if(BUILD_DBUS_SUPPORT)
    list(APPEND TEST_SOURCES
        test_sd_bus_connection.cpp
        test_sd_bus_match.cpp
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

# Build only the testable source files into the test binary directly.
# Avoids pulling libubootenv / libarchive transitively from fs_update_static.
target_sources(fs_updater_lib_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/logger/LoggerEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/subprocess/subprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/Descriptor.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/Sha256Hasher.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/UpdateStoreV2.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/RaucInstallSink.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/inspect_bundle.cpp
    ${CMAKE_SOURCE_DIR}/src/dbus/progress_interpolation.cpp
)

target_link_libraries(fs_updater_lib_tests PRIVATE PkgConfig::JSONCPP PkgConfig::BOTAN2)

if(BUILD_DBUS_SUPPORT)
    pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
    target_sources(fs_updater_lib_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/dbus/sd_bus_connection.cpp
        ${CMAKE_SOURCE_DIR}/src/dbus/sd_bus_match.cpp
    )
    target_link_libraries(fs_updater_lib_tests PRIVATE PkgConfig::SYSTEMD)
endif()

target_link_libraries(fs_updater_lib_tests PRIVATE GTest::gtest_main)

add_test(NAME fs_updater_lib_tests COMMAND fs_updater_lib_tests)
