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

set(TEST_COMPILE_OPTIONS -Wall -Wextra -Wpedantic $<$<BOOL:${FSUP_WERROR}>:-Werror>)

set(TEST_SOURCES
    test_logger.cpp
    test_subprocess.cpp
    test_base_exception.cpp
    test_allowed_uboot_variable_states.cpp
    test_scoped_directory.cpp
)

add_executable(fs_updater_lib_tests ${TEST_SOURCES})

target_compile_features(fs_updater_lib_tests PRIVATE cxx_std_17)
set_target_properties(fs_updater_lib_tests PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(fs_updater_lib_tests PRIVATE ${TEST_COMPILE_OPTIONS})

target_include_directories(fs_updater_lib_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)

# Build only the testable source files into the test binary directly.
# Avoids pulling libubootenv / botan / libarchive transitively from fs_update_static.
target_sources(fs_updater_lib_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/logger/LoggerEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/logger/LoggerSinkStdout.cpp
    ${CMAKE_SOURCE_DIR}/src/subprocess/subprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/utils.cpp
)

# Bootstate needs the libubootenv and botan-2 headers on the include path
# (declarations only): UBoot.cpp is linked against the in-memory stub of the
# libuboot C API in stub_libuboot.cpp, and handleUpdate.h pulls the botan
# declarations through updateApplication.h. Point LIBUBOOT_INCLUDE_DIR /
# BOTAN2_INCLUDE_DIR at them when they are not installed system-wide.
find_path(LIBUBOOT_INCLUDE_DIR libuboot.h)
find_path(BOTAN2_INCLUDE_DIR botan/x509cert.h PATH_SUFFIXES botan-2)
if(LIBUBOOT_INCLUDE_DIR AND BOTAN2_INCLUDE_DIR)
    target_sources(fs_updater_lib_tests PRIVATE
        test_bootstate.cpp
        stub_libuboot.cpp
        ${CMAKE_SOURCE_DIR}/src/uboot_interface/UBoot.cpp
        ${CMAKE_SOURCE_DIR}/src/handle_update/handleUpdate.cpp
        ${CMAKE_SOURCE_DIR}/src/handle_update/updateDefinitions.cpp
        ${CMAKE_SOURCE_DIR}/src/handle_update/updateBase.cpp
        ${CMAKE_SOURCE_DIR}/src/logger/LoggerHandler.cpp
        ${CMAKE_SOURCE_DIR}/src/logger/LoggerSinkEmpty.cpp
    )
    target_include_directories(fs_updater_lib_tests SYSTEM PRIVATE
        ${LIBUBOOT_INCLUDE_DIR}
        ${BOTAN2_INCLUDE_DIR}
    )
    # The work directory is a compile-time path; the tests get one of their
    # own inside the build tree and drive the install marker in it.
    target_compile_definitions(fs_updater_lib_tests PRIVATE
        TEMP_ADU_WORK_DIR="${CMAKE_CURRENT_BINARY_DIR}/adu_work"
    )
elseif(FSUP_ALLOW_SKIPPED_BOOTSTATE_TESTS)
    message(WARNING "libuboot.h or botan-2 headers not found - the Bootstate tests are skipped")
else()
    message(FATAL_ERROR
        "libuboot.h or botan-2 headers not found - the Bootstate tests cannot be built. "
        "Set LIBUBOOT_INCLUDE_DIR / BOTAN2_INCLUDE_DIR, or -DFSUP_ALLOW_SKIPPED_BOOTSTATE_TESTS=ON "
        "to run the rest of the suite without them.")
endif()

target_link_libraries(fs_updater_lib_tests PRIVATE GTest::gtest_main)

add_test(NAME fs_updater_lib_tests COMMAND fs_updater_lib_tests)
