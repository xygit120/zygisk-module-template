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

# ==================== LSPlant 配置（已适配 workflow） ====================
set(LSPLANT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant/lsplant/src/main/jni/include)

if(EXISTS ${LSPLANT_INCLUDE_DIR}/lsplant.hpp)
    message(STATUS "Found LSPlant headers at: ${LSPLANT_INCLUDE_DIR}")
    include_directories(${LSPLANT_INCLUDE_DIR})
else()
    message(FATAL_ERROR "lsplant.hpp not found at ${LSPLANT_INCLUDE_DIR}. Workflow clone step may have issues.")
endif()

target_link_libraries(bootloader_spoofer
    log
)

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY \( {CMAKE_CURRENT_SOURCE_DIR}/zygisk/ \){ANDROID_ABI}
)