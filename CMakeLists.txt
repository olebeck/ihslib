cmake_minimum_required(VERSION 3.0)

project(ihslib C)

set(CMAKE_C_STANDARD 11)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/scripts)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(IHSLIB_SAMPLES "Build Samples" ON)
    option(IHSLIB_CODEGEN "Code Generator" ON)
else ()
    option(IHSLIB_SAMPLES "Build Samples" OFF)
    option(IHSLIB_CODEGEN "Code Generator" OFF)
endif ()

option(IHSLIB_SANITIZE_ADDRESS "Link Address Sanitizer" OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 sdl2)
pkg_check_modules(HIDAPI hidapi-hidraw)

if (NOT PROTOBUF_C_FOUND)
    pkg_check_modules(PROTOBUF_C libprotobuf-c REQUIRED)
endif ()

if (NOT MBEDTLS_FOUND)
    find_package(MbedTLS)
endif ()

if (NOT MBEDTLS_FOUND)
    find_package(OpenSSL)
endif ()

add_library(ihslib
        src/base.c
        src/crc32.c
        src/crc32c.c
        src/ihs_timer.c
        src/ihs_ip.c
        src/ihs_buffer.c
        src/ihs_queue.c
        src/ihs_arraylist.c
        src/ihs_enumeration.c
        src/ihs_enumeration_ll.c
        src/ihs_enumeration_array.c
        src/client/client.c
        src/client/discovery.c
        src/client/authorization.c
        src/client/streaming.c
        src/session/session.c
        src/session/packet.c
        src/session/frame.c
        src/session/window.c
        src/session/frame_crypto.c
        src/session/channels/channel.c
        src/session/channels/ch_discovery.c
        src/session/channels/ch_control.c
        src/session/channels/ch_control_authentication.c
        src/session/channels/ch_control_negotiation.c
        src/session/channels/ch_control_keepalive.c
        src/session/channels/ch_control_audio.c
        src/session/channels/ch_control_video.c
        src/session/channels/ch_data.c
        src/session/channels/ch_data_audio.c
        src/session/channels/video/frame_h264.c
        src/session/channels/video/frame_hevc.c
        src/session/channels/video/partial_frames.c
        src/session/channels/video/ch_data_video.c
        src/session/channels/control/control_cursor.c
        src/session/channels/control/control_input.c
        src/session/channels/control/control_hid.c
        src/session/channels/ch_stats.c
        src/hid/device.c
        src/hid/provider.c
        src/hid/manager.c
        src/hid/report.c)

if (SDL2_VERSION VERSION_GREATER_EQUAL 2.0.18)
    set(IHSLIB_SDL2_HIDAPI 1)
    target_compile_definitions(ihslib PUBLIC IHSLIB_SDL2_HIDAPI)
endif ()

if (HIDAPI_FOUND)
    set(IHSLIB_HIDAPI 1)
    target_compile_definitions(ihslib PUBLIC IHSLIB_HIDAPI)
endif ()

target_include_directories(ihslib PUBLIC include PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(src/protobuf)
target_link_libraries(ihslib PRIVATE ihs-protobuf)

add_subdirectory(src/platforms)
target_include_directories(ihs-platforms PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ihslib PRIVATE ihs-platforms)

if(WINRT)
    target_link_libraries(ihslib PRIVATE pthread) # clock_gettime
endif()

if (MBEDTLS_FOUND)
    target_sources(ihslib PRIVATE src/crypto/impl_mbedtls.c)
    target_include_directories(ihslib PRIVATE SYSTEM ${MBEDTLS_INCLUDE_DIRS})
    target_link_libraries(ihslib PUBLIC ${MBEDCRYPTO_LIBRARY})
elseif (OPENSSL_FOUND)
    target_sources(ihslib PRIVATE src/crypto/impl_openssl.c)
endif ()

add_subdirectory(src/hid)

if (IHSLIB_SANITIZE_ADDRESS)
    add_compile_definitions(IHSLIB_SANITIZE_ADDRESS)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address -static-libasan)
    link_libraries(asan)
endif ()

if (IHSLIB_SAMPLES)
    add_subdirectory(samples)
endif ()

# Don't include tools and samples if used as library
if (IHSLIB_CODEGEN)
    find_program(PROTOC_C protoc-c)
    find_program(PYTHON python3)

    set(PROTOBUF_FILES
            protobuf/discovery.proto
            protobuf/remoteplay.proto
            protobuf/hiddevices.proto)
    add_custom_target(protoc-gen
            COMMAND ${PROTOC_C} --c_out=src ${PROTOBUF_FILES}
            COMMAND ${PYTHON} scripts/protobuf-src-fixup.py src/protobuf
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif ()

# Don't include tests by default if used as library
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif ()