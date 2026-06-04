cmake_minimum_required(VERSION 3.22.1)

project(bootloaderspoofer)

add_subdirectory(shadowhook)

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

target_include_directories(zygisk_bootloader PRIVATE shadowhook/include)

find_library(log-lib log)
target_link_libraries(zygisk_bootloader ${log-lib} shadowhook)
