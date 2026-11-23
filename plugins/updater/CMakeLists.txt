cmake_minimum_required(VERSION 3.20)

project(etaHENPluginUpdater VERSION 1.0.0 LANGUAGES C)

# Plugin identity. Keep these values stable after publishing a plugin.
set(ETA_PLUGIN_TARGET updater)
set(ETA_PLUGIN_ID UPDT00001)
set(ETA_PLUGIN_VERSION 1.00)
set(ETA_PLUGIN_NAME "Plugin Updater")

if(NOT ETA_PLUGIN_ID MATCHES "^[A-Za-z][A-Za-z][A-Za-z][A-Za-z][0-9][0-9][0-9][0-9][0-9]$")
    message(FATAL_ERROR "ETA_PLUGIN_ID must contain four letters followed by five digits")
endif()

set(ETAHEN_PLUGIN_SDK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH
    "etaHEN plugin SDK checkout")
if(NOT EXISTS "${ETAHEN_PLUGIN_SDK_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "plugin SDK checkout not found: ${ETAHEN_PLUGIN_SDK_SOURCE}")
endif()
add_subdirectory(
    "${ETAHEN_PLUGIN_SDK_SOURCE}"
    "${CMAKE_BINARY_DIR}/_deps/etahen-plugin-sdk-build"
    EXCLUDE_FROM_ALL)

# libcurl + CA bundle from the PS5 payload SDK (same as the etaHEN util).
set(ETA_CURL_ROOT "${PS5_PAYLOAD_SDK}/target/user/homebrew")
set(ETA_CURL_CA_BUNDLE "${ETA_CURL_ROOT}/etc/ca-bundle.crt")
if(NOT EXISTS "${ETA_CURL_ROOT}/include/curl/curl.h" OR
   NOT EXISTS "${ETA_CURL_ROOT}/lib/libcurl.a" OR
   NOT EXISTS "${ETA_CURL_CA_BUNDLE}")
    message(FATAL_ERROR "PS5 payload SDK libcurl and CA bundle required (looked in ${ETA_CURL_ROOT})")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/plugin_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/plugin_config.h"
    @ONLY)

eta_add_plugin(
    NAME ${ETA_PLUGIN_TARGET}
    TITLE_ID ${ETA_PLUGIN_ID}
    VERSION ${ETA_PLUGIN_VERSION}
    SOURCES
        source/main.c
        source/plugin_descriptor.c
        source/plugin_settings.c
        source/plugin_ui.c
        source/downloader.c
        source/progress_file.c
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${ETA_CURL_ROOT}/include"
    LIBRARIES
        SceLibcInternal
        SceSystemService
        SceNotification
        SceNetCtl
        SceNet
        kernel_sys
        curl
        psl
        ssl
        crypto
        zstd
        z)

target_link_directories(${ETA_PLUGIN_TARGET} PRIVATE "${ETA_CURL_ROOT}/lib")
target_compile_definitions(${ETA_PLUGIN_TARGET} PRIVATE
    CURL_STATICLIB
    ETA_CURL_CA_BUNDLE="${ETA_CURL_CA_BUNDLE}")
# The CA bundle is embedded with .incbin (invisible to the dependency scanner).
set_source_files_properties(source/downloader.c PROPERTIES
    OBJECT_DEPENDS "${ETA_CURL_CA_BUNDLE}")

# The daemon identifies plugins by file name: name the ELF after the plugin ID.
set_target_properties(${ETA_PLUGIN_TARGET} PROPERTIES
    OUTPUT_NAME "${ETA_PLUGIN_ID}.elf")
