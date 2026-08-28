cmake_minimum_required(VERSION 3.18)
project(fus_updater_lib VERSION 2.2.6 LANGUAGES C CXX)

include(GNUInstallDirs)

# ==============================================================================
# Options
# ==============================================================================

set(OPTIMIZE_FOR "SIZE" CACHE STRING "Release optimization: SIZE or SPEED")
set_property(CACHE OPTIMIZE_FOR PROPERTY STRINGS SIZE SPEED)

set(UBOOT_ENV_NAND "mtd5" CACHE STRING "MTD part name of u-boot env. block")
set(UBOOT_ENV_MMC "mmcblk2boot0" CACHE STRING "MMC part name of u-boot env. block")
set(FSUP_RAUC_SCRATCH "/rw_fs/.cache/update.fw" CACHE PATH
    "Persistent-storage scratch path used by the v2.0 streaming reader to stage the RAUC firmware bundle before `rauc install`")
set(FSUP_APP_IMG_STORE "/rw_fs/root/application/" CACHE PATH
    "Persistent-storage directory holding app_a.squashfs/app_b.squashfs (+ their verity sidecars) and the .incoming staging file the RAUC app-bundle install hook writes. Override per-BSP (e.g. meta-fus sets this to FUS_UPDATE_APP_IMG_DIR, /data/app/images) so the hook script and this library agree on one path. Trailing slash required (matches applicationUpdate's STANDARD_APP_IMG_STORE convention).")
set(update_version_type "string" CACHE STRING "Data type for fw/app version")

# Source identity reported by library_source_id(). The project version moves
# rarely, so on its own it cannot tell two builds apart; a recipe passes the
# revision it pinned (-DFUS_SOURCE_ID=${SRCREV}). Falls back to the working
# tree's own description, and to "unknown" outside a checkout, so the value is
# never silently empty.
set(FUS_SOURCE_ID "" CACHE STRING "Source revision reported by the library")
if(NOT FUS_SOURCE_ID)
    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --always --dirty
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE FUS_SOURCE_ID
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
endif()
if(NOT FUS_SOURCE_ID)
    set(FUS_SOURCE_ID "unknown")
endif()
option(fs_version_compare    "Enable FS version comparison"           OFF)
option(BUILD_DBUS_SUPPORT    "Enable D-Bus based RAUC communication"  ON)
option(BUILD_RAUC_SCRATCH_OVERRIDE
       "Honor the runtime rauc_scratch_path arg on FSUpdate::update_image() (OFF = always use compile-time DEFAULT_RAUC_SCRATCH_PATH)"
       ON)
option(BUILD_TESTING         "Build unit tests (native build)"        OFF)
option(BUILD_MAIN_TARGET     "Build library targets (OFF = tests only)" ON)
option(FSUP_WERROR           "Treat compiler warnings as errors (-Werror)" ON)
option(ENABLE_SANITIZERS     "Enable ASan and UBSan (native test builds)" OFF)
option(BUILD_FUZZING         "Build libFuzzer targets (native, clang only)" OFF)

# Botan2: manual include path
set(BOTAN2 "" CACHE STRING "Include path to the headers for botan-2")
message(STATUS "Using Botan2 include directory: ${BOTAN2}")

# Validate options
if(NOT OPTIMIZE_FOR MATCHES "^(SIZE|SPEED)$")
    message(FATAL_ERROR "OPTIMIZE_FOR must be SIZE or SPEED, got: ${OPTIMIZE_FOR}")
endif()

if("${update_version_type}" STREQUAL "string")
    set(UPDATE_VERSION_TYPE_STRING 1)
    set(UPDATE_VERSION_TYPE_UINT64 0)
elseif("${update_version_type}" STREQUAL "uint64")
    set(UPDATE_VERSION_TYPE_STRING 0)
    set(UPDATE_VERSION_TYPE_UINT64 1)
else()
    message(FATAL_ERROR "Unknown update_version_type: ${update_version_type}")
endif()

# Override CMake's default Release flags (-O3 -DNDEBUG) to avoid conflicting -O levels.
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG" CACHE STRING "" FORCE)

# Global so the test binaries (and fetched gtest) share the sanitizer runtime.
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ==============================================================================
# Generated config header
# ==============================================================================

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/config.h.in
    ${CMAKE_BINARY_DIR}/include/${PROJECT_NAME}/config.h
    @ONLY
)

# ==============================================================================
# Tests-only mode (skip main library targets when cross-compile libs unavailable)
# ==============================================================================

# Shared with tests/CMakeLists.txt and fuzz/CMakeLists.txt: the subset of
# fs-updater-lib source files that build natively without libubootenv /
# libarchive (both pull in only through fs_update_static, the cross-compiled
# main target below). One list, so the gtest binary and the fuzz targets
# can never drift on "what actually builds natively".
set(LIB_TESTABLE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/util/posix_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/error_mapping.cpp
    ${CMAKE_SOURCE_DIR}/src/logger/LoggerEntry.cpp
    ${CMAKE_SOURCE_DIR}/src/logger/LoggerSinkStdout.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/Descriptor.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/Sha256Hasher.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/UpdateContainerReader.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/sources/UpdateContainerSource.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/sources/UpdateSourceRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/sources/RaucBundleSource.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/sources/ApplicationImageSource.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/RaucInstallSink.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/app_bundle_install.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/inspect_bundle.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/applicationImage.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/cert_image_verifier.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/handleUpdate.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/updateBase.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/updateApplication.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/updateDefinitions.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/reboot_state.cpp
    ${CMAKE_SOURCE_DIR}/src/handle_update/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/logger/LoggerHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/logger/LoggerSinkEmpty.cpp
    ${CMAKE_SOURCE_DIR}/src/dbus/progress_interpolation.cpp
)

if(BUILD_TESTING AND NOT BUILD_MAIN_TARGET)
    enable_testing()
    add_subdirectory(tests)
    if(BUILD_FUZZING)
        add_subdirectory(fuzz)
    endif()
    return()
endif()

# ==============================================================================
# Dependencies
# ==============================================================================

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(BOTAN2_PKG botan-2)
    if(BOTAN2_PKG_FOUND)
        message(STATUS "Found botan-2 via pkg-config")
    endif()
endif()

if(BUILD_DBUS_SUPPORT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
    message(STATUS "D-Bus support enabled (libsystemd ${SYSTEMD_VERSION})")
endif()

# ==============================================================================
# Sources
# ==============================================================================

file(GLOB_RECURSE SOURCES_LOGGER CONFIGURE_DEPENDS src/logger/*.cpp)
file(GLOB_RECURSE HEADERS_LOGGER CONFIGURE_DEPENDS src/logger/*.h)
file(GLOB_RECURSE SOURCES_RAUC CONFIGURE_DEPENDS src/rauc/*.cpp)
file(GLOB_RECURSE HEADERS_RAUC CONFIGURE_DEPENDS src/rauc/*.h)
# rauc_dbus_client lives in src/dbus/ (BUILD_DBUS_SUPPORT only); exclude the stub
list(REMOVE_ITEM HEADERS_RAUC "${CMAKE_CURRENT_SOURCE_DIR}/src/rauc/rauc_dbus_client.h")
file(GLOB_RECURSE SOURCES_UBOOT CONFIGURE_DEPENDS src/uboot_interface/*.cpp)
file(GLOB_RECURSE HEADERS_UBOOT CONFIGURE_DEPENDS src/uboot_interface/*.h)
file(GLOB_RECURSE SOURCES_HANDLE_UPDATE CONFIGURE_DEPENDS src/handle_update/*.cpp)
file(GLOB_RECURSE HEADERS_HANDLE_UPDATE CONFIGURE_DEPENDS src/handle_update/*.h)
file(GLOB_RECURSE SOURCES_UTIL CONFIGURE_DEPENDS src/util/*.cpp)
file(GLOB_RECURSE HEADERS_UTIL CONFIGURE_DEPENDS src/util/*.h)
file(GLOB HEADERS_BASE_EXCEPTION CONFIGURE_DEPENDS src/BaseException.h)
# error.h is included by the installed error_mapping.h, so shipping one without
# the other leaves a header no consumer can include. Found when the service
# tried to use the classifier instead of writing its own catch ladder.
file(GLOB HEADERS_ERROR CONFIGURE_DEPENDS src/error.h)
file(GLOB HEADERS_SOURCE_ID CONFIGURE_DEPENDS src/library_source_id.h)
file(GLOB SOURCES_SOURCE_ID CONFIGURE_DEPENDS src/library_source_id.cpp)

set(SOURCES
    ${SOURCES_LOGGER}
    ${SOURCES_RAUC}
    ${SOURCES_UBOOT}
    ${SOURCES_HANDLE_UPDATE}
    ${SOURCES_UTIL}
    ${SOURCES_SOURCE_ID}
)

set(HEADERS
    ${HEADERS_LOGGER}
    ${HEADERS_RAUC}
    ${HEADERS_UBOOT}
    ${HEADERS_HANDLE_UPDATE}
    ${HEADERS_UTIL}
    ${HEADERS_BASE_EXCEPTION}
    ${HEADERS_SOURCE_ID}
)

if(BUILD_DBUS_SUPPORT)
    file(GLOB_RECURSE SOURCES_DBUS CONFIGURE_DEPENDS src/dbus/*.cpp)
    file(GLOB_RECURSE HEADERS_DBUS CONFIGURE_DEPENDS src/dbus/*.h)
    list(APPEND SOURCES ${SOURCES_DBUS})
    list(APPEND HEADERS ${HEADERS_DBUS})
endif()

# ==============================================================================
# Targets
# ==============================================================================

add_library(fs_update_static STATIC ${SOURCES})
add_library(fs_update_shared SHARED ${SOURCES})

foreach(_target fs_update_static fs_update_shared)
    target_compile_features(${_target} PUBLIC cxx_std_17)
    set_target_properties(${_target} PROPERTIES CXX_EXTENSIONS OFF)

    target_include_directories(${_target} PUBLIC ${CMAKE_BINARY_DIR}/include)
    # The source tree itself, so internal units can cross-reference subdirs
    # (e.g. handle_update/ -> util/) without "../.." relative includes. PRIVATE:
    # consumers use the installed fs_update_framework/ layout, not src/.
    target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    if(BOTAN2)
        target_include_directories(${_target} PUBLIC ${BOTAN2})
    endif()

    if(BOTAN2_PKG_FOUND)
        target_include_directories(${_target} PUBLIC ${BOTAN2_PKG_INCLUDE_DIRS})
        target_link_directories(${_target} PUBLIC ${BOTAN2_PKG_LIBRARY_DIRS})
    endif()

    # ------------------------------------------------------------------
    # Compiler and linker flags
    # ------------------------------------------------------------------

    target_compile_options(${_target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        # Warnings are errors by default; disable with -DFSUP_WERROR=OFF after a
        # toolchain bump that introduces a new warning. Deps are -isystem
        # (pkg-config) and thus exempt from -Werror by default.
        $<$<BOOL:${FSUP_WERROR}>:-Werror>
        # Release optimizations
        $<$<CONFIG:Release>:$<IF:$<STREQUAL:${OPTIMIZE_FOR},SPEED>,-O2,-Os>>
        $<$<CONFIG:Release>:-flto>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )

    target_link_options(${_target} PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-flto>
        $<$<CONFIG:Release>:-s>
    )
endforeach()

if(BUILD_DBUS_SUPPORT)
    foreach(_target fs_update_static fs_update_shared)
        target_link_libraries(${_target} PUBLIC PkgConfig::SYSTEMD)
    endforeach()
endif()

if(BUILD_RAUC_SCRATCH_OVERRIDE)
    foreach(_target fs_update_static fs_update_shared)
        target_compile_definitions(${_target} PRIVATE BUILD_RAUC_SCRATCH_OVERRIDE=1)
    endforeach()
endif()

# Which fw_env.config the default-environment constructor opens. Empty leaves
# the header's constant standing, so an ordinary build is unchanged; a harness
# sets it to drive this code against a prepared environment instead of a
# device's.
set(UBOOT_CONFIG_PATH "" CACHE STRING "fw_env.config path compiled into the default environment")
if(UBOOT_CONFIG_PATH)
    foreach(_target fs_update_static fs_update_shared)
        target_compile_definitions(${_target} PRIVATE UBOOT_CONFIG_PATH="${UBOOT_CONFIG_PATH}")
    endforeach()
endif()

# Output names
set_target_properties(fs_update_shared PROPERTIES OUTPUT_NAME "fs_updater" SUFFIX ".so.1")
set_target_properties(fs_update_static PROPERTIES OUTPUT_NAME "fs_updater")

# ==============================================================================
# Install
# ==============================================================================

install(TARGETS fs_update_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS fs_update_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES ${HEADERS_HANDLE_UPDATE}
        DESTINATION "include/fs_update_framework/handle_update")
install(FILES ${HEADERS_RAUC}
        DESTINATION "include/fs_update_framework/rauc")
install(FILES ${HEADERS_UBOOT}
        DESTINATION "include/fs_update_framework/uboot_interface")
install(FILES ${HEADERS_LOGGER}
        DESTINATION "include/fs_update_framework/logger")
install(FILES ${HEADERS_ERROR}
        DESTINATION "include/fs_update_framework/")
install(FILES ${HEADERS_BASE_EXCEPTION}
        DESTINATION "include/fs_update_framework/")

install(FILES ${HEADERS_SOURCE_ID}
        DESTINATION "include/fs_update_framework/")

install(DIRECTORY ${CMAKE_BINARY_DIR}/include/${PROJECT_NAME}
        DESTINATION include)

if(BUILD_DBUS_SUPPORT)
    install(FILES ${HEADERS_DBUS}
            DESTINATION "include/fs_update_framework/dbus")
endif()

# ==============================================================================
# Tests
# ==============================================================================

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
