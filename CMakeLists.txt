cmake_minimum_required(VERSION 3.5)
# set the project name
project(fus_updater_lib)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

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

add_library(fs_update_static STATIC ${SOURCES})
add_library(fs_update_shared SHARED ${SOURCES})

target_include_directories(fs_update_static PUBLIC ${BOTAN2})
target_include_directories(fs_update_shared PUBLIC ${BOTAN2})

set_target_properties(fs_update_shared PROPERTIES OUTPUT_NAME "fs_updater")
set_target_properties(fs_update_static PROPERTIES OUTPUT_NAME "fs_updater")
set_target_properties(fs_update_shared PROPERTIES SUFFIX ".so.1")

install(TARGETS fs_update_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS fs_update_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
# Following libraries have to be added as dependencies (keep sequence of linked libraries)
# libjsoncpp_static.a 
# botan-2.a 
# pthread
# ubootenv.a 
# z.a 
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
#add_executable(fus_updater_lib "src/main.cpp")
#target_include_directories(fus_updater_lib PUBLIC ${BOTAN2})
#target_link_libraries(fus_updater_lib fs_update_static libjsoncpp_static.a botan-2.a pthread ubootenv.a z.a inicpp.a)
