cmake_minimum_required(VERSION 3.22)

project(shadowhook)

enable_language(ASM)

if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(ARCH "arm64")
    set(ARCH_LINK_FLAGS "-Wl,-z,max-page-size=16384")
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(ARCH "arm")
    set(ARCH_LINK_FLAGS "")
endif()

set(TARGET "shadowhook")

file(GLOB SRC 
    *.c 
    arch/${ARCH}/*.c 
    arch/${ARCH}/*.S 
    common/*.c 
    third_party/*/*.c
)

add_library(${TARGET} SHARED ${SRC})

target_compile_features(${TARGET} PRIVATE c_std_11)

# 关键修改：降低警告严格度
target_compile_options(${TARGET} PRIVATE 
    -Wall -Wextra
    -Wno-unsafe-buffer-usage
    -Wno-declaration-after-statement
    -Wno-missing-declarations
    -Wno-sign-compare
    -Wno-format
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-parameter
    -Os -ffunction-sections -fdata-sections
)

target_include_directories(${TARGET} PRIVATE 
    . 
    include 
    arch/${ARCH} 
    common 
    third_party/xdl 
    third_party/bsd 
    third_party/lss
)

target_link_libraries(${TARGET} log)

target_link_options(${TARGET} PRIVATE 
    ${ARCH_LINK_FLAGS}
    -Wl,--gc-sections
)

# shadowhook_nothing（保持不变）
set(TARGET "shadowhook_nothing")
add_library(${TARGET} SHARED nothing/sh_nothing.c)
target_compile_options(${TARGET} PRIVATE -Oz -fno-ident -fno-unwind-tables -fno-asynchronous-unwind-tables)
target_link_options(${TARGET} PRIVATE ${ARCH_LINK_FLAGS} -Wl,--strip-all -Wl,--as-needed -Wl,--hash-style=sysv -Wl,--build-id=none -Wl,-nostdlib -nostartfiles -nodefaultlibs)