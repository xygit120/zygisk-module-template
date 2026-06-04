cmake_minimum_required(VERSION 3.18.1)

project("zygisk_bootloader")

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

target_link_libraries(zygisk_bootloader
    log
    android
)
