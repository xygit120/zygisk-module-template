cmake_minimum_required(VERSION 3.22.1)

project(bootloaderspoofer)

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

find_library(log-lib log)
target_link_libraries(zygisk_bootloader ${log-lib} shadowhook)
