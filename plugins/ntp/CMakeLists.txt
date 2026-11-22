cmake_minimum_required(VERSION 3.20)

project(etaHENNtpPlugin VERSION 1.0.0 LANGUAGES C)

# Plugin identity. Keep these values stable after publishing a plugin.
set(ETA_PLUGIN_TARGET ntp)
set(ETA_PLUGIN_ID NTPS00001)
set(ETA_PLUGIN_VERSION 1.00)
set(ETA_PLUGIN_NAME "NTP Time Sync")

if(NOT ETA_PLUGIN_ID MATCHES "^[A-Za-z][A-Za-z][A-Za-z][A-Za-z][0-9][0-9][0-9][0-9][0-9]$")
    message(FATAL_ERROR
        "ETA_PLUGIN_ID must contain four letters followed by five digits")
endif()
if(NOT ETA_PLUGIN_VERSION MATCHES "^[0-9]+\\.[0-9][0-9]$")
    message(FATAL_ERROR "ETA_PLUGIN_VERSION must use the N.NN format")
endif()

# The plugin SDK is vendored next to this plugin (plugins/sdk).
set(ETAHEN_PLUGIN_SDK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH
    "etaHEN plugin SDK checkout")
if(NOT EXISTS "${ETAHEN_PLUGIN_SDK_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "plugin SDK checkout not found: ${ETAHEN_PLUGIN_SDK_SOURCE}")
endif()
add_subdirectory(
    "${ETAHEN_PLUGIN_SDK_SOURCE}"
    "${CMAKE_BINARY_DIR}/_deps/etahen-plugin-sdk-build"
    EXCLUDE_FROM_ALL)

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
        source/ntp_i18n.c
        source/ntp_sync.c
        source/ntp_service.c
        source/plugin_descriptor.c
        source/plugin_settings.c
        source/plugin_ui.c
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    LIBRARIES
        SceLibcInternal
        SceSystemService
        SceNet
        SceNotification
        kernel_sys)
