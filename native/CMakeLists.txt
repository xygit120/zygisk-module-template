cmake_minimum_required(VERSION 3.22.1)
project(BootloaderSpoofer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID_ABI)
    set(ANDROID_ABI arm64-v8a)
endif()

# ==================== Dobby ====================
set(DOBBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/dobby)

if(EXISTS ${DOBBY_DIR}/CMakeLists.txt)
    add_subdirectory(${DOBBY_DIR} dobby)
    set(HAS_DOBBY TRUE)
    message(STATUS "Dobby enabled")
else()
    message(FATAL_ERROR "Dobby not found!")
endif()

# ==================== LSPlant ====================
set(LSPLANT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lsplant)
set(LSPLANT_SRC ${LSPLANT_DIR}/lsplant/src/main/jni/lsplant.cc)

if(EXISTS ${LSPLANT_SRC})
    add_library(lsplant STATIC ${LSPLANT_SRC})
    target_include_directories(lsplant PUBLIC
        ${LSPLANT_DIR}/lsplant/src/main/jni
        ${LSPLANT_DIR}/lsplant/src/main/jni/include
    )
    set(HAS_LSPLANT TRUE)
    message(STATUS "LSPlant enabled")
else()
    message(FATAL_ERROR "LSPlant not found!")
endif()

# ==================== 主模块 ====================
add_library(bootloader_spoofer SHARED main.cpp)

target_include_directories(bootloader_spoofer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOBBY_DIR}/include          # 显式加上 Dobby 头文件路径
)

target_link_libraries(bootloader_spoofer PRIVATE
    log
    android
    lsplant
    dobby_static
)

if(HAS_DOBBY)
    target_compile_definitions(bootloader_spoofer PRIVATE HAS_DOBBY=1)
endif()

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/zygisk/${ANDROID_ABI}"
)