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
find_path(LSPLANT_INCLUDE_DIR
    NAMES lsplant.hpp
    PATHS 
        ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant/lsplant/src/main/jni/include
        ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant/include
        ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant
        ${CMAKE_CURRENT_SOURCE_DIR}                    # 支持手动复制到根目录
    NO_DEFAULT_PATH
)

if(LSPLANT_INCLUDE_DIR)
    message(STATUS "✅ Found LSPlant headers at: ${LSPLANT_INCLUDE_DIR}")
    include_directories(${LSPLANT_INCLUDE_DIR})
else()
    message(STATUS "❌ lsplant.hpp not found in standard paths. Checking external/lsplant and project root...")
    execute_process(
        COMMAND find ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant -name lsplant.hpp
        OUTPUT_VARIABLE LSPLANT_HEADERS
    )
    execute_process(
        COMMAND find ${CMAKE_CURRENT_SOURCE_DIR} -maxdepth 3 -name lsplant.hpp
        OUTPUT_VARIABLE ROOT_HEADERS
    )
    message(FATAL_ERROR "lsplant.hpp not found!\n\nIn external/lsplant:\n\( {LSPLANT_HEADERS}\n\nIn project (maxdepth 3):\n \){ROOT_HEADERS}")
endif()

target_link_libraries(bootloader_spoofer
    log
)

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY \( {CMAKE_CURRENT_SOURCE_DIR}/zygisk/ \){ANDROID_ABI}
)