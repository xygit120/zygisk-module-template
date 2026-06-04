cmake_minimum_required(VERSION 3.28)
project(BootloaderSpoofer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 注意：不要在全局盲目强制开启 SCAN_FOR_MODULES
# 让 LSPlant 子项目内部自己处理，并且编译时必须使用 Ninja 

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

# ==================== LSPlant（调用官方 CMake） ====================
set(LSPLANT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant)
# LSPlant 真正的构建配置在 lsplant/src/main/jni 下
if(EXISTS ${LSPLANT_DIR}/lsplant/src/main/jni/CMakeLists.txt)
    # 优雅地引入官方完整的模块化构建逻辑
    add_subdirectory(${LSPLANT_DIR}/lsplant/src/main/jni lsplant_build)
    set(HAS_LSPLANT TRUE)
    message(STATUS "LSPlant enabled via official CMake configuration")
else()
    message(FATAL_ERROR "LSPlant CMakeLists.txt not found! 请检查路径或子模块是否完整")
endif()

# ==================== 主模块 ====================
add_library(bootloader_spoofer SHARED main.cpp)

target_include_directories(bootloader_spoofer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOBBY_DIR}/include
    ${LSPLANT_DIR}/lsplant/src/main/jni/include   # 确保能正确引入 lsplant.hpp
)

target_link_libraries(bootloader_spoofer PRIVATE
    log
    android
    lsplant_static                                # LSPlant 官方静态库的目标名叫 lsplant_static
    dobby
)

if(HAS_DOBBY)
    target_compile_definitions(bootloader_spoofer PRIVATE HAS_DOBBY=1)
endif()

# 修复了损坏的变量语法 
set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/zygisk/${ANDROID_ABI}"
)
