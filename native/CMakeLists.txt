cmake_minimum_required(VERSION 3.22.1)
project(BootloaderSpoofer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID_ABI)
    set(ANDROID_ABI arm64-v8a)
endif()

# ==================== Dobby（强制静态库） ====================
set(DOBBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/dobby)

if(EXISTS ${DOBBY_DIR}/CMakeLists.txt)
    set(DOBBY_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(DOBBY_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(DOBBY_BUILD_KERNEL ON CACHE BOOL "" FORCE)

    add_subdirectory(${DOBBY_DIR} dobby)
    set(HAS_DOBBY TRUE)
    message(STATUS "Dobby enabled (static)")
else()
    message(FATAL_ERROR "Dobby not found! Run: git clone https://github.com/LSPosed/Dobby.git native/external/dobby")
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
    message(FATAL_ERROR "LSPlant not found! Run: git clone https://github.com/LSPosed/LSPlant.git native/external/lsplant")
endif()

# ==================== 主模块 ====================
add_library(bootloader_spoofer SHARED
    main.cpp
)

target_include_directories(bootloader_spoofer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bootloader_spoofer
    log
    android
    lsplant
    dobby
)

if(HAS_DOBBY)
    target_compile_definitions(bootloader_spoofer PRIVATE HAS_DOBBY=1)
endif()

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY \( {CMAKE_CURRENT_SOURCE_DIR}/../zygisk/ \){ANDROID_ABI}
)