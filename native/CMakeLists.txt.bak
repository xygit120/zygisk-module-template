cmake_minimum_required(VERSION 3.22.1)
project(bootloader-spoofer)

macro(strip_and_move NAME)
    add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${DEBUG_SYMBOLS_PATH}
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${NAME}> ${DEBUG_SYMBOLS_PATH}/$<TARGET_FILE_NAME:${NAME}>
            COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:${NAME}>
    )
endmacro()

set(CXX_FLAGS "${CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS}")

# ==================== LSPlant 支持 ====================
# 请把 LSPlant 源码 clone 到 external/lsplant
# git clone https://github.com/LSPosed/LSPlant.git external/lsplant
set(LSPLANT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external/lsplant/lsplant/src/main/jni/include)

if(EXISTS ${LSPLANT_INCLUDE_DIR}/lsplant.hpp)
    message(STATUS "Found LSPlant headers at: ${LSPLANT_INCLUDE_DIR}")
    include_directories(${LSPLANT_INCLUDE_DIR})
else()
    message(WARNING "LSPlant not found! Please run: git clone https://github.com/LSPosed/LSPlant.git external/lsplant")
endif()

add_library(${MODULE_NAME} SHARED main.cpp)
target_link_libraries(${MODULE_NAME} PRIVATE log)
strip_and_move(${MODULE_NAME})
