cmake_minimum_required(VERSION 3.20)

project(etaHENOrbitManagerPlugin VERSION 1.0.0 LANGUAGES C)

# Plugin identity. Keep these values stable after publishing a plugin.
set(ETA_PLUGIN_TARGET orbitmgr)
set(ETA_PLUGIN_ID ORBM00001)
set(ETA_PLUGIN_VERSION 1.00)
set(ETA_PLUGIN_NAME "Orbit Store Manager")

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

# Vendored, self-contained copies of etaHEN-core helpers (see README in
# third_party/eta_common): the private-elfldr socket client and a trimmed
# sysctl-based process lookup, so this plugin does not depend on core-only
# internal libraries.
set(ORBITMGR_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eta_common")
set(ORBITMGR_COMMON_SOURCES
    "${ORBITMGR_COMMON_DIR}/elfldr_remote.c"
    "${ORBITMGR_COMMON_DIR}/orbit_proc.c")

eta_add_plugin(
    NAME ${ETA_PLUGIN_TARGET}
    TITLE_ID ${ETA_PLUGIN_ID}
    VERSION ${ETA_PLUGIN_VERSION}
    SOURCES
        source/main.c
        source/orbit_service.c
        source/orbit_ffpkg.c
        source/plugin_descriptor.c
        source/plugin_settings.c
        source/plugin_ui.c
        ${ORBITMGR_COMMON_SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eta_common"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    LIBRARIES
        SceLibcInternal
        SceSystemService
        kernel_sys)

set_target_properties(${ETA_PLUGIN_TARGET} PROPERTIES
    OUTPUT_NAME "${ETA_PLUGIN_ID}.elf")
target_compile_definitions(${ETA_PLUGIN_TARGET} PRIVATE
    VERSION_TAG="etaHEN Orbit Manager plugin ${ETA_PLUGIN_VERSION}")
set_source_files_properties(${ORBITMGR_COMMON_SOURCES} PROPERTIES
    COMPILE_FLAGS "-Wno-error -Wno-unused-function -Wno-unused-parameter")
