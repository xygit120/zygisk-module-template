cmake_minimum_required(VERSION 3.22.1)
project(BootloaderSpoofer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID_ABI)
    set(ANDROID_ABI arm64-v8a)
endif()

add_library(bootloader_spoofer SHARED
    jni/zygisk_main.cpp
)

# ==================== LSPlant 配置 ====================
# 你已经把 lsplant.hpp 放在项目根目录，直接 include 当前目录即可
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "Using lsplant.hpp from project root directory")

target_link_libraries(bootloader_spoofer
    log
)

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY \( {CMAKE_CURRENT_SOURCE_DIR}/zygisk/ \){ANDROID_ABI}
)