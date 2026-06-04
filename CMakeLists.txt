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

# ==================== LSPlant 正确头文件路径 ====================
set(LSPLANT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant/lsplant/src/main/jni)

if(EXISTS ${LSPLANT_ROOT}/include/lsplant.hpp)
    message(STATUS "Found LSPlant headers at: ${LSPLANT_ROOT}/include")
    include_directories(${LSPLANT_ROOT}/include)
    include_directories(${LSPLANT_ROOT})   # 有些模块文件可能需要
else()
    message(FATAL_ERROR "lsplant.hpp not found! Please check LSPlant structure.")
endif()

target_link_libraries(bootloader_spoofer
    log
)

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/zygisk/${ANDROID_ABI}
)