cmake_minimum_required(VERSION 3.5)
# set the project name
project(fus_updater_lib)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# set(CMAKE_CXX_FLAGS " -Wall -Wextra -flto -O2")

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
            "src/include/fs_updater.cpp"
)

set(HEADERS "src/include/fs_updater.hpp"
)

add_library(fs_update_static STATIC ${SOURCES})
target_link_libraries(fs_update_static libjsoncpp_static.a)
target_link_libraries(fs_update_static botan-2.a)
target_link_libraries(fs_update_static z.a)
target_link_libraries(fs_update_static ubootenv.a)
target_link_libraries(fs_update_static inicpp.a)


add_library(fs_update_shared SHARED ${SOURCES})
target_link_libraries(fs_update_shared libjsoncpp.so)
target_link_libraries(fs_update_shared botan-2.so)
target_link_libraries(fs_update_shared z.so)
target_link_libraries(fs_update_shared ubootenv.so)
target_link_libraries(fs_update_static inicpp.so)

set_target_properties(fs_update_shared PROPERTIES OUTPUT_NAME "fs_updater")
set_target_properties(fs_update_static PROPERTIES OUTPUT_NAME "fs_updater")
set_target_properties(fs_update_shared PROPERTIES SUFFIX ".so.1")

install(TARGETS fs_update_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS fs_update_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES "src/include/fs_updater.hpp" 
        DESTINATION "include/fs_update_framework" # target directory
)
# Set executable
#add_executable(fus_updater_lib "src/main.cpp")
#target_link_libraries(fus_updater_lib fs_update_static)
