cmake_minimum_required(VERSION 3.28)
project(BootloaderSpoofer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 关键修改：关闭模块扫描（NDK Clang 目前不支持 CMake 的 C++ Modules 依赖扫描）
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT ANDROID_ABI)
    set(ANDROID_ABI arm64-v8a)
endif()

# ==================== Dobby 静态 ====================
set(DOBBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/dobby)
if(EXISTS ${DOBBY_DIR}/CMakeLists.txt)
    set(DOBBY_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${DOBBY_DIR} dobby)
    set(HAS_DOBBY TRUE)
    message(STATUS "Dobby enabled (static)")
else()
    message(FATAL_ERROR "Dobby not found!")
endif()

# ==================== LSPlant ====================
set(LSPLANT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant)
if(EXISTS ${LSPLANT_DIR}/lsplant/src/main/jni/CMakeLists.txt)
    add_subdirectory(${LSPLANT_DIR}/lsplant/src/main/jni lsplant_build)
    set(HAS_LSPLANT TRUE)
    message(STATUS "LSPlant enabled via official CMake configuration")
else()
    message(FATAL_ERROR "LSPlant CMakeLists.txt not found!")
endif()

# ==================== 主模块 ====================
add_library(bootloader_spoofer SHARED main.cpp)

target_include_directories(bootloader_spoofer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOBBY_DIR}/include
    ${LSPLANT_DIR}/lsplant/src/main/jni/include
)

target_link_libraries(bootloader_spoofer PRIVATE
    log
    android
    lsplant_static
    dobby
)

if(HAS_DOBBY)
    target_compile_definitions(bootloader_spoofer PRIVATE HAS_DOBBY=1)
endif()

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/zygisk/${ANDROID_ABI}"
)