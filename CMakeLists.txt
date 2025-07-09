cmake_minimum_required(VERSION 3.5)
# set the project name
project(fus_updater_lib)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This configuration is needed, because Botan means it is useaful to put every header file seperatly
# include_directories(SYSTEM, "botan/botan-3/")
SET(BOTAN2 CACHE STRING "Include path to the headers for botan-2")
message("-- Add include directory for botan2: ${BOTAN2}")

set(SOURCES "src/subprocess/subprocess.cpp"
            "src/uboot_interface/UBoot.cpp"
            "src/rauc/rauc_handler.cpp" 
            "src/handle_update/utils.cpp"
            "src/handle_update/updateDefinitions.cpp"
            "src/handle_update/updateBase.cpp"
            "src/handle_update/updateFirmware.cpp" 
            "src/handle_update/updateApplication.cpp"
            "src/handle_update/fsupdate.cpp"
            "src/handle_update/handleUpdate.cpp"
            "src/handle_update/applicationImage.cpp"
            "src/logger/LoggerHandler.cpp"
            "src/logger/LoggerEntry.cpp"
            "src/logger/LoggerSinkStdout.cpp"
            "src/logger/LoggerSinkEmpty.cpp"
)

set(HEADERS_HANDLE_UPDATE 
        "src/handle_update/fsupdate.h;src/handle_update/handleUpdate.h;src/handle_update/updateApplication.h;src/handle_update/updateBase.h;src/handle_update/updateDefinitions.h;src/handle_update/updateFirmware.h;src/handle_update/applicationImage.h;src/handle_update/utils.h;"
)

set(HEADERS_RAUC
        "src/rauc/rauc_handler.h;"
)

set(HEADERS_SUBPROCESS
        "src/subprocess/subprocess.h;"
)

set(HEADERS_UBOOT
        "src/uboot_interface/UBoot.h;"
)

set(HEADERS_LOGGER
        "src/logger/LoggerEntry.h;src/logger/LoggerHandler.h;src/logger/LoggerLevel.h;src/logger/LoggerSinkBase.h;src/logger/LoggerSinkStdout.h;src/logger/LoggerSinkEmpty.h;"
)

set(HEADERS_BASE_EXCEPTION
        "src/BaseException.h"
)

set (UBOOT_ENV_NAND
        "mtd5"
        CACHE STRING "MTD part name of u-boot env. block"
)

set (UBOOT_ENV_MMC
        "mmcblk2boot0"
        CACHE STRING "MMC part name of u-boot env. block"
)

set(update_version_type "string" CACHE STRING "data type fw, app version")
option(fs_version_compare "set fs_version_compare to ON if fs version compare is to be used, set to OFF to not use this" OFF)


if (${fs_version_compare})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_FS_VERSION_COMPARE")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_FS_VERSION_COMPARE")
endif ()

if("${update_version_type}" STREQUAL "string")
        add_definitions (-DUPDATE_VERSION_TYPE_STRING)
endif ()

if("${update_version_type}" STREQUAL "uint64")
        add_definitions (-DUPDATE_VERSION_TYPE_UINT64)
endif ()


#add_definitions (-DFIRMWARE_VERSION_REGEX_STRING="${FIRMWARE_VERSION_REGEX_STRING}")
#add_definitions (-DAPPLICATION_VERSION_REGEX_STRING="${APPLICATION_VERSION_REGEX_STRING}")

add_definitions (-DUBOOT_ENV_NAND="${UBOOT_ENV_NAND}")
add_definitions (-DUBOOT_ENV_MMC="${UBOOT_ENV_MMC}")

add_library(fs_update_static STATIC ${SOURCES})
add_library(fs_update_shared SHARED ${SOURCES})

target_include_directories(fs_update_static PUBLIC ${BOTAN2})
target_include_directories(fs_update_shared PUBLIC ${BOTAN2})

# Try to find pkg-config, but make it optional for cross-compile environments
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(BOTAN2 botan-2)
    if(BOTAN2_FOUND)
        include_directories(${BOTAN2_INCLUDE_DIRS})
        link_directories(${BOTAN2_LIBRARY_DIRS})
        message(STATUS "Found botan-2 via pkg-config")
    endif()
else()
    message(STATUS "pkg-config not found, using manual botan-2 configuration")
endif()

# Ensure BOTAN2 include path is always set
if(NOT BOTAN2_INCLUDE_DIRS AND BOTAN2)
    include_directories(${BOTAN2})
endif()
set_target_properties(fs_update_shared PROPERTIES OUTPUT_NAME "fs_updater")
set_target_properties(fs_update_static PROPERTIES OUTPUT_NAME "fs_updater")
set_target_properties(fs_update_shared PROPERTIES SUFFIX ".so.1")

install(TARGETS fs_update_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS fs_update_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
# Following libraries have to be added as dependencies (keep sequence of linked libraries)
# libjsoncpp_static.a 
# botan-2
# pthread
# ubootenv
# z
# inicpp.a
install(FILES ${HEADERS_HANDLE_UPDATE}
        DESTINATION "include/fs_update_framework/handle_update" # target directory
)

install(FILES ${HEADERS_SUBPROCESS}
        DESTINATION "include/fs_update_framework/subprocess" # target directory
)

install(FILES ${HEADERS_RAUC}
        DESTINATION "include/fs_update_framework/rauc" # target directory
)

install(FILES ${HEADERS_UBOOT}
        DESTINATION "include/fs_update_framework/uboot_interface" # target directory
)

install(FILES ${HEADERS_LOGGER}
        DESTINATION "include/fs_update_framework/logger" # target directory
)

install(FILES ${HEADERS_BASE_EXCEPTION}
        DESTINATION "include/fs_update_framework/" # target directory
)

# Set executable
#set(CMAKE_CXX_FLAGS " -Wall -Wextra -Wpedantic -Os -s")
#set(CMAKE_CXX_FLAGS " -Wall -Wextra -Wpedantic -O -g3")
#add_executable(fus_updater_lib "src/main.cpp")
#target_include_directories(fus_updater_lib PUBLIC ${BOTAN2})
#target_link_libraries(fus_updater_lib fs_update_static libjsoncpp_static.a botan-2.a pthread ubootenv.a z.a inicpp.a)
