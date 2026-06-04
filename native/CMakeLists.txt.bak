cmake_minimum_required(VERSION 3.22.1)

project(bootloaderspoofer)

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

# === 使用预编译的 ShadowHook ===
add_library(shadowhook SHARED IMPORTED)

set_target_properties(shadowhook PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/arm64-v8a/libshadowhook.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

find_library(log-lib log)

target_link_libraries(zygisk_bootloader
    ${log-lib}
    shadowhook
)