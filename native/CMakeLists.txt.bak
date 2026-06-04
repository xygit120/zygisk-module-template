cmake_minimum_required(VERSION 3.22.1)

project(bootloaderspoofer)

# 1. 在引入第三方库前，注入编记选项：关闭 unsafe-buffer 警告，且不把警告当错误
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unsafe-buffer-usage -Wno-error")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unsafe-buffer-usage -Wno-error")

# 2. 精准指向 ShadowHook 源码目录
add_subdirectory(shadowhook/shadowhook/src/main/cpp)

add_library(zygisk_bootloader SHARED
    BootloaderSpoofer.cpp
)

# 3. 修正头文件包含路径
target_include_directories(zygisk_bootloader PRIVATE shadowhook/shadowhook/src/main/cpp/include)

find_library(log-lib log)
target_link_libraries(zygisk_bootloader ${log-lib} shadowhook)
