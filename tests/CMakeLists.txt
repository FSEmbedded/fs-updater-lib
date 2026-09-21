include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(TEST_COMPILE_OPTIONS -Wall -Wextra -Wpedantic)

set(TEST_SOURCES
    test_logger.cpp
    test_subprocess.cpp
    test_base_exception.cpp
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
    ${CMAKE_SOURCE_DIR}/src/subprocess/subprocess.cpp
)

target_link_libraries(fs_updater_lib_tests PRIVATE GTest::gtest_main)

add_test(NAME fs_updater_lib_tests COMMAND fs_updater_lib_tests)
