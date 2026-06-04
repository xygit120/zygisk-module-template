cmake_minimum_required(VERSION 3.22.1)
project(BootloaderSpoofer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID_ABI)
    set(ANDROID_ABI arm64-v8a)
endif()

# ==================== Dobby 集成 ====================
set(DOBBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/dobby)

if(EXISTS ${DOBBY_DIR}/CMakeLists.txt)
    message(STATUS "Dobby found at ${DOBBY_DIR}")

    # Dobby 构建选项
    set(DOBBY_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(DOBBY_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(DOBBY_BUILD_KERNEL ON CACHE BOOL "" FORCE)

    add_subdirectory(${DOBBY_DIR} dobby)

    set(HAS_DOBBY TRUE)
else()
    message(WARNING "Dobby not found! Please run: git clone https://github.com/LSPosed/Dobby.git native/external/dobby")
    set(HAS_DOBBY FALSE)
endif()
# ===================================================

add_library(bootloader_spoofer SHARED
    main.cpp
)

target_include_directories(bootloader_spoofer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bootloader_spoofer
    log
    android
)

if(HAS_DOBBY)
    target_link_libraries(bootloader_spoofer dobby)
    target_compile_definitions(bootloader_spoofer PRIVATE HAS_DOBBY=1)
    message(STATUS "Dobby enabled for LSPlant inline hooking")
endif()

set_target_properties(bootloader_spoofer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../zygisk/${ANDROID_ABI}
)
