cmake_minimum_required(VERSION 3.20)

project(arps_host_debug LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)

set(ARPS_NATIVE_CLIENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../native-client"
    "${CMAKE_CURRENT_BINARY_DIR}/native-client")

set(ARPS_LZ4_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/lz4")
set(ARPS_NATIVE_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../native-client/src")

set(ARPS_SDL2_TARGET "")
foreach(candidate SDL2::SDL2 SDL2 SDL2-shared SDL2::SDL2-shared)
    if(TARGET "${candidate}" AND NOT ARPS_SDL2_TARGET)
        set(ARPS_SDL2_TARGET "${candidate}")
    endif()
endforeach()
if(NOT ARPS_SDL2_TARGET)
    message(FATAL_ERROR "SDL2 FetchContent did not provide a usable SDL2 target")
endif()
set(ARPS_SDL2_RUNTIME_TARGET "${ARPS_SDL2_TARGET}")
get_target_property(ARPS_SDL2_ALIASED_TARGET "${ARPS_SDL2_TARGET}" ALIASED_TARGET)
if(ARPS_SDL2_ALIASED_TARGET)
    set(ARPS_SDL2_RUNTIME_TARGET "${ARPS_SDL2_ALIASED_TARGET}")
endif()

add_executable(arps-host-debug cli/main.cpp)
target_link_libraries(arps-host-debug PRIVATE arps_native_client "${ARPS_SDL2_TARGET}")
target_compile_options(arps-host-debug PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wall>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wextra>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-pedantic>
)

if(WIN32)
    get_target_property(ARPS_SDL2_TARGET_TYPE "${ARPS_SDL2_RUNTIME_TARGET}" TYPE)
    if(NOT ARPS_SDL2_TARGET_TYPE STREQUAL "INTERFACE_LIBRARY"
            AND NOT ARPS_SDL2_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
        add_custom_command(TARGET arps-host-debug POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:${ARPS_SDL2_RUNTIME_TARGET}>"
                "$<TARGET_FILE_DIR:arps-host-debug>"
            VERBATIM)
    endif()
endif()

add_executable(arps-mock-source mock-source/main.cpp)
target_link_libraries(arps-mock-source PRIVATE arps_native_client)
target_include_directories(arps-mock-source PRIVATE
    "${ARPS_NATIVE_SRC_DIR}"
    "${ARPS_LZ4_DIR}"
)
target_compile_options(arps-mock-source PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wall>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wextra>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-pedantic>
)
