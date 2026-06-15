cmake_minimum_required(VERSION 3.22.1)

project(arps_lz4 C)

add_library(lz4arps SHARED
        lz4_jni.c
        ${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/lz4/lz4.c)

target_include_directories(lz4arps PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/lz4)

target_compile_definitions(lz4arps PRIVATE
        LZ4_HEAPMODE=1)
