cmake_minimum_required(VERSION 3.28)
project(BootloaderSpoofer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)     # 开启模块扫描

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

# ==================== LSPlant（Modules） ====================
set(LSPLANT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant)
set(LSPLANT_SRC ${LSPLANT_DIR}/lsplant/src/main/jni/lsplant.cc)

if(EXISTS ${LSPLANT_SRC})
    add_library(lsplant STATIC ${LSPLANT_SRC})
    target_include_directories(lsplant PUBLIC
        ${LSPLANT_DIR}/lsplant/src/main/jni
        ${LSPLANT_DIR}/lsplant/src/main/jni/include
    )
    target_compile_options(lsplant PRIVATE -fmodules -fcxx-modules)
    target_compile_features(lsplant PRIVATE cxx_std_20)
    set(HAS_LSPLANT TRUE)
    message(STATUS "LSPlant enabled (with C++ Modules)")
else()
    message(FATAL_ERROR "LSPlant not found!")
endif()

# ==================== 主模块 ====================
add_library(bootloader_spoofer SHARED main.cpp)

target_include_directories(bootloader_spoofer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOBBY_DIR}/include
)

target_link_libraries(bootloader_spoofer PRIVATE
    log
    android
    lsplant
    dobby
)

if(HAS_DOBBY)
    target_compile_definitions(bootloader_spoofer PRIVATE HAS_DOBBY=1)
endif()

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "\( {CMAKE_CURRENT_BINARY_DIR}/zygisk/ \){ANDROID_ABI}"
)