cmake_minimum_required(VERSION 3.20)

project(etaHENDashboardPlugin VERSION 1.0.0 LANGUAGES C)

# Plugin identity. Keep these values stable after publishing a plugin.
set(ETA_PLUGIN_TARGET dashboard)
set(ETA_PLUGIN_ID DASH00001)
set(ETA_PLUGIN_VERSION 1.00)
set(ETA_PLUGIN_NAME "Dashboard")

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
        source/dash_appdb.c
        source/plugin_descriptor.c
        source/plugin_settings.c
        source/plugin_ui.c
        source/wh_embed.c
        source/wh_runner.c
        third_party/sqlite3.c
        third_party/wh_bridge/util.c
        third_party/wh_bridge/remote.c
        third_party/wh_bridge/hookpoint.c
        third_party/wh_bridge/prelude.c
        third_party/wh_bridge/server.c
        third_party/wh_bridge/tiles.c
        third_party/wh_bridge/applist.c
        third_party/wh_bridge/install.c
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wh_bridge"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    LIBRARIES
        SceLibcInternal
        SceSystemService
        SceUserService
        SceNet
        SceNotification
        kernel_sys)

# sqlite3 amalgamation: relax warnings (third-party code) and trim features.
set_source_files_properties(third_party/sqlite3.c PROPERTIES
    COMPILE_OPTIONS "-w;-Wno-error;-O2"
    COMPILE_DEFINITIONS "SQLITE_THREADSAFE=0;SQLITE_OMIT_LOAD_EXTENSION;SQLITE_OMIT_SHARED_CACHE;SQLITE_TEMP_STORE=3")

# Welcome Hub bridge (TOSanthony/dashboard sources): third-party style code,
# built with -Wall only (no -Wextra/-Werror).
set(_wh_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wh_bridge")
set_source_files_properties(
    "${_wh_dir}/util.c" "${_wh_dir}/remote.c" "${_wh_dir}/hookpoint.c"
    "${_wh_dir}/prelude.c" "${_wh_dir}/server.c" "${_wh_dir}/tiles.c"
    "${_wh_dir}/applist.c" "${_wh_dir}/install.c"
    source/wh_runner.c
    PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-unused-parameter;-Wno-sign-compare;-Wno-missing-field-initializers")
set_source_files_properties(source/wh_embed.c PROPERTIES
    COMPILE_DEFINITIONS "WH_EMBED_DIR=\"${_wh_dir}\""
    OBJECT_DEPENDS "${_wh_dir}/bridge.hbc;${_wh_dir}/loader.js")
