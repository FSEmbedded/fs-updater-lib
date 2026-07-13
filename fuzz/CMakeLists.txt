# libFuzzer targets. Requires clang — GCC has no -fsanitize=fuzzer
# equivalent. Only invoked via `./scripts/build.sh fuzz`, which configures a
# separate build_fuzz/ directory with clang as the compiler; BUILD_FUZZING
# stays OFF (and this directory unused) for the ordinary gcc-based `test`
# build, so this is genuinely additive — it never affects the existing
# native test path.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
        "BUILD_FUZZING requires clang (got ${CMAKE_CXX_COMPILER_ID}); "
        "use ./scripts/build.sh fuzz, which sets this up.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
pkg_check_modules(BOTAN2 REQUIRED IMPORTED_TARGET botan-2)
find_package(ZLIB REQUIRED)

set(FUZZ_SANITIZE_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)

# No fuzz target touches D-Bus code (they exercise pure parsers), so
# unlike tests/CMakeLists.txt this never links libsystemd — one less
# native-host dependency for a build that's already gated on having clang.
foreach(target_name IN ITEMS fuzz_app_image_header fuzz_v2_container)
    add_executable(${target_name} ${target_name}.cpp ${LIB_TESTABLE_SOURCES})
    target_compile_features(${target_name} PRIVATE cxx_std_17)
    set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/include
    )
    target_compile_options(${target_name} PRIVATE -Wall -Wextra ${FUZZ_SANITIZE_FLAGS})
    target_link_options(${target_name} PRIVATE ${FUZZ_SANITIZE_FLAGS})
    target_link_libraries(${target_name} PRIVATE
        PkgConfig::JSONCPP PkgConfig::BOTAN2 ZLIB::ZLIB)
endforeach()
