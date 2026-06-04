cmake_minimum_required(VERSION 3.22.1)

# 【核心修正】强制此项目只编译 64 位，忽略 32 位的编译请求
set(ANDROID_ABI "arm64-v8a" CACHE STRING "Force 64-bit only" FORCE)

project(bootloaderspoofer)

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

# ShadowHook 预编译库
add_library(shadowhook SHARED IMPORTED)

# 既然只打 64 位，这里保持原路径即可
set_target_properties(shadowhook PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/arm64-v8a/libshadowhook.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

find_library(log-lib log)

target_link_libraries(zygisk_bootloader
    ${log-lib}
    shadowhook
)
