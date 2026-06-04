cmake_minimum_required(VERSION 3.22.1)

project(bootloaderspoofer)

# 1. 修改路径，指向 shadowhook 仓库内部的 shadowhook 子目录
add_subdirectory(shadowhook/shadowhook)

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

# 2. 同样需要修正头文件的包含路径，多加一层 shadowhook
target_include_directories(zygisk_bootloader PRIVATE shadowhook/shadowhook/include)

find_library(log-lib log)
target_link_libraries(zygisk_bootloader ${log-lib} shadowhook)
