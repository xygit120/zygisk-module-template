cmake_minimum_required(VERSION 3.22.1)

project(bootloaderspoofer)

# 1. 精准指向真正包含 CMakeLists.txt 的原生 C++ 源码目录
add_subdirectory(shadowhook/shadowhook/src/main/cpp)

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

# 2. 同步修正头文件的包含路径到 src/main/cpp/include
target_include_directories(zygisk_bootloader PRIVATE shadowhook/shadowhook/src/main/cpp/include)

find_library(log-lib log)
target_link_libraries(zygisk_bootloader ${log-lib} shadowhook)
