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
if(FUS_LEGACY_IMAGE_SUPPORT)
    pkg_check_modules(BOTAN2 REQUIRED IMPORTED_TARGET botan-2)
endif()
find_package(ZLIB REQUIRED)

set(FUZZ_SANITIZE_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)

# No fuzz target touches D-Bus code (they exercise pure parsers), so
# unlike tests/CMakeLists.txt this never links libsystemd — one less
# native-host dependency for a build that's already gated on having clang.
#
# LIB_TESTABLE_SOURCES is compiled once here, under the fuzzer+ASan+UBSan
# instrumentation every target needs, and linked into each executable as an
# OBJECT library. Keeping the sanitizer flags on the shared objects is intentional:
# coverage feedback from library internals is what makes the fuzzer effective.
add_library(fuzz_common OBJECT ${LIB_TESTABLE_SOURCES})
target_compile_features(fuzz_common PUBLIC cxx_std_17)
set_target_properties(fuzz_common PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(fuzz_common PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)

# updateApplication.cpp (LIB_TESTABLE_SOURCES) needs boost::property_tree
# headers, only available via the SDK sysroot. Symlink just the boost/ subtree:
# exposing the target glibc headers to a native compile breaks it.
if(DEFINED ENV{SDK_ROOT})
    set(SDK_BOOST_SRC "$ENV{SDK_ROOT}/sysroots/cortexa53-fslc-linux/usr/include/boost")
    if(EXISTS "${SDK_BOOST_SRC}")
        set(BOOST_INCLUDE_STAGE "${CMAKE_CURRENT_BINARY_DIR}/boost-include")
        file(MAKE_DIRECTORY "${BOOST_INCLUDE_STAGE}")
        if(NOT EXISTS "${BOOST_INCLUDE_STAGE}/boost")
            file(CREATE_LINK "${SDK_BOOST_SRC}" "${BOOST_INCLUDE_STAGE}/boost" SYMBOLIC)
        endif()
        target_include_directories(fuzz_common PUBLIC ${BOOST_INCLUDE_STAGE})
    endif()
endif()
target_compile_options(fuzz_common PUBLIC -Wall -Wextra ${FUZZ_SANITIZE_FLAGS})
target_link_options(fuzz_common PUBLIC ${FUZZ_SANITIZE_FLAGS})
target_link_libraries(fuzz_common PUBLIC PkgConfig::JSONCPP ZLIB::ZLIB)
if(FUS_LEGACY_IMAGE_SUPPORT)
    target_link_libraries(fuzz_common PUBLIC PkgConfig::BOTAN2)
endif()

# The two state targets carry no external dependency of their own -- they
# exercise the durable alphabet and its bitfield, which are header functions --
# but they are built through the same recipe so every target shares one set of
# instrumentation flags.
set(FUZZ_TARGETS fuzz_reboot_state_decoder fuzz_update_bits)
# The two format parsers go with the units they parse (HeaderParser is
# defined in cert_image_verifier.cpp); scripts/build.sh skips them the same way.
if(FUS_LEGACY_IMAGE_SUPPORT)
    list(APPEND FUZZ_TARGETS fuzz_app_image_header fuzz_v2_container)
endif()
foreach(target_name IN LISTS FUZZ_TARGETS)
    add_executable(${target_name} ${target_name}.cpp)
    target_link_libraries(${target_name} PRIVATE fuzz_common)
endforeach()
