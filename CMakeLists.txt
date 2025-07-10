cmake_minimum_required(VERSION 3.5)
project(fus_updater_lib VERSION 1.1.0)

# Compiler settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Botan2: manual include path
set(BOTAN2 "" CACHE STRING "Include path to the headers for botan-2")
message(STATUS "Using Botan2 include directory: ${BOTAN2}")

# Configuration options
set(UBOOT_ENV_NAND "mtd5" CACHE STRING "MTD part name of u-boot env. block")
set(UBOOT_ENV_MMC "mmcblk2boot0" CACHE STRING "MMC part name of u-boot env. block")
set(update_version_type "string" CACHE STRING "Data type for fw/app version")
option(fs_version_compare "Enable FS version comparison" OFF)

# Set exactly one of the following flags to 1
if("${update_version_type}" STREQUAL "string")
    set(UPDATE_VERSION_TYPE_STRING 1)
    set(UPDATE_VERSION_TYPE_UINT64 0)
elseif("${update_version_type}" STREQUAL "uint64")
    set(UPDATE_VERSION_TYPE_STRING 0)
    set(UPDATE_VERSION_TYPE_UINT64 1)
else()
    message(FATAL_ERROR "Unknown update_version_type: ${update_version_type}")
endif()

# Generate config header
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/config.h.in
    ${CMAKE_BINARY_DIR}/include/${PROJECT_NAME}/config.h
    @ONLY
)

# Collect source and header files per module
file(GLOB_RECURSE SOURCES_LOGGER CONFIGURE_DEPENDS src/logger/*.cpp)
file(GLOB_RECURSE HEADERS_LOGGER CONFIGURE_DEPENDS src/logger/*.h)
file(GLOB_RECURSE SOURCES_RAUC CONFIGURE_DEPENDS src/rauc/*.cpp)
file(GLOB_RECURSE HEADERS_RAUC CONFIGURE_DEPENDS src/rauc/*.h)
file(GLOB_RECURSE SOURCES_UBOOT CONFIGURE_DEPENDS src/uboot_interface/*.cpp)
file(GLOB_RECURSE HEADERS_UBOOT CONFIGURE_DEPENDS src/uboot_interface/*.h)
file(GLOB_RECURSE SOURCES_HANDLE_UPDATE CONFIGURE_DEPENDS src/handle_update/*.cpp)
file(GLOB_RECURSE HEADERS_HANDLE_UPDATE CONFIGURE_DEPENDS src/handle_update/*.h)
file(GLOB_RECURSE SOURCES_SUBPROCESS CONFIGURE_DEPENDS src/subprocess/*.cpp)
file(GLOB_RECURSE HEADERS_SUBPROCESS CONFIGURE_DEPENDS src/subprocess/*.h)
file(GLOB HEADERS_BASE_EXCEPTION CONFIGURE_DEPENDS src/BaseException.h)

# Combine all sources and headers
set(SOURCES
    ${SOURCES_LOGGER}
    ${SOURCES_RAUC}
    ${SOURCES_UBOOT}
    ${SOURCES_HANDLE_UPDATE}
    ${SOURCES_SUBPROCESS}
)

set(HEADERS
    ${HEADERS_LOGGER}
    ${HEADERS_RAUC}
    ${HEADERS_UBOOT}
    ${HEADERS_HANDLE_UPDATE}
    ${HEADERS_SUBPROCESS}
    ${HEADERS_BASE_EXCEPTION}
)

# Build libraries
add_library(fs_update_static STATIC ${SOURCES})
add_library(fs_update_shared SHARED ${SOURCES})

# Target-specific include directories for config.h
target_include_directories(fs_update_static PUBLIC ${CMAKE_BINARY_DIR}/include)
target_include_directories(fs_update_shared PUBLIC ${CMAKE_BINARY_DIR}/include)

# Add Botan2 include directory if set manually
if(BOTAN2)
    target_include_directories(fs_update_static PUBLIC ${BOTAN2})
    target_include_directories(fs_update_shared PUBLIC ${BOTAN2})
endif()

# Optionally use pkg-config to locate Botan2
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(BOTAN2_PKG botan-2)
    if(BOTAN2_PKG_FOUND)
        target_include_directories(fs_update_static PUBLIC ${BOTAN2_PKG_INCLUDE_DIRS})
        target_include_directories(fs_update_shared PUBLIC ${BOTAN2_PKG_INCLUDE_DIRS})
        target_link_directories(fs_update_static PUBLIC ${BOTAN2_PKG_LIBRARY_DIRS})
        target_link_directories(fs_update_shared PUBLIC ${BOTAN2_PKG_LIBRARY_DIRS})
        message(STATUS "Found botan-2 via pkg-config")
    endif()
endif()

# Set output names and shared library suffix
set_target_properties(fs_update_shared PROPERTIES OUTPUT_NAME "fs_updater" SUFFIX ".so.1")
set_target_properties(fs_update_static PROPERTIES OUTPUT_NAME "fs_updater")

# Install targets and headers
install(TARGETS fs_update_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS fs_update_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Install headers in module-specific directories (maintaining old structure)
install(FILES ${HEADERS_HANDLE_UPDATE}
        DESTINATION "include/fs_update_framework/handle_update")
install(FILES ${HEADERS_SUBPROCESS}
        DESTINATION "include/fs_update_framework/subprocess")
install(FILES ${HEADERS_RAUC}
        DESTINATION "include/fs_update_framework/rauc")
install(FILES ${HEADERS_UBOOT}
        DESTINATION "include/fs_update_framework/uboot_interface")
install(FILES ${HEADERS_LOGGER}
        DESTINATION "include/fs_update_framework/logger")
install(FILES ${HEADERS_BASE_EXCEPTION}
        DESTINATION "include/fs_update_framework/")

# Install generated config header
install(DIRECTORY ${CMAKE_BINARY_DIR}/include/${PROJECT_NAME}
        DESTINATION include)