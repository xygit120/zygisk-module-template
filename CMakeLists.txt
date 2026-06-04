cmake_minimum_required(VERSION 3.22.1)

project(BootloaderSpoofer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android NDK settings
if(NOT ANDROID_ABI)
    set(ANDROID_ABI arm64-v8a)
endif()

add_library(bootloader_spoofer SHARED
    jni/zygisk_main.cpp
)

# ==================== LSPlant 配置 ====================
# 请把 LSPlant 源码放到 external/lsplant 目录
# 或者使用 git submodule add https://github.com/LSPosed/LSPlant.git external/lsplant

set(LSPLANT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant)

if(EXISTS ${LSPLANT_DIR}/lsplant/src/main/jni/include/lsplant.hpp)
    message(STATUS "Found LSPlant at ${LSPLANT_DIR}")
    include_directories(${LSPLANT_DIR}/lsplant/src/main/jni/include)
else()
    message(WARNING "LSPlant not found! Please put LSPlant source in external/lsplant")
endif()

target_link_libraries(bootloader_spoofer
    log
)

# 输出到 zygisk/arm64-v8a 目录
set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/zygisk/${ANDROID_ABI}
)

