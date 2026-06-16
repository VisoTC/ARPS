cmake_minimum_required(VERSION 3.16)

project(arps_native_client LANGUAGES C CXX)

option(ARPS_NATIVE_CLIENT_BUILD_TESTS "Build native-client tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ARPS_LZ4_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/lz4")

add_library(arps_native_client STATIC
    src/protocol.cpp
    src/receiver.cpp
    src/socket_compat.cpp
    "${ARPS_LZ4_DIR}/lz4.c"
)

target_include_directories(arps_native_client
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE
        "${ARPS_LZ4_DIR}"
)

target_compile_options(arps_native_client PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wall>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wextra>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Werror>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-pedantic>
    $<$<COMPILE_LANG_AND_ID:C,MSVC>:/W3>
    $<$<COMPILE_LANG_AND_ID:C,Clang,AppleClang,GNU>:-Wall>
    $<$<COMPILE_LANG_AND_ID:C,Clang,AppleClang,GNU>:-Wextra>
)

if(WIN32)
    target_link_libraries(arps_native_client PUBLIC ws2_32)
endif()

if(ARPS_NATIVE_CLIENT_BUILD_TESTS)
    add_executable(receiver_tests tests/receiver_tests.cpp)
    target_link_libraries(receiver_tests PRIVATE arps_native_client)
    target_include_directories(receiver_tests PRIVATE "${ARPS_LZ4_DIR}")
    target_compile_options(receiver_tests PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wall>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wextra>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Werror>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-pedantic>
    )

    enable_testing()
    add_test(NAME receiver_tests COMMAND $<TARGET_FILE:receiver_tests>)
endif()
