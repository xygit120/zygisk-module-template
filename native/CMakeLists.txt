cmake_minimum_required(VERSION 3.22.1)

project(bootloaderspoofer)

# 添加 ShadowHook 子模块
add_subdirectory(shadowhook)

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

# 包含 ShadowHook 的头文件
target_include_directories(zygisk_bootloader PRIVATE shadowhook/include)

find_library(log-lib log)
target_link_libraries(zygisk_bootloader ${log-lib} shadowhook)
