cmake_minimum_required(VERSION 3.20)

project(etaHENFtpServerPlugin VERSION 1.0.0 LANGUAGES C)

set(ETA_PLUGIN_TARGET ftpsrv)
set(ETA_PLUGIN_ID FTPS00001)
set(ETA_PLUGIN_VERSION 1.00)
set(ETA_PLUGIN_NAME "FTP Server")

# The plugin SDK is vendored next to this plugin (plugins/sdk).
set(ETAHEN_PLUGIN_SDK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH
    "etaHEN plugin SDK checkout")
if(NOT EXISTS "${ETAHEN_PLUGIN_SDK_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "ETAHEN_PLUGIN_SDK_SOURCE is not an SDK checkout: ${ETAHEN_PLUGIN_SDK_SOURCE}")
endif()
add_subdirectory(
    "${ETAHEN_PLUGIN_SDK_SOURCE}"
    "${CMAKE_BINARY_DIR}/_deps/etahen-plugin-sdk-build"
    EXCLUDE_FROM_ALL)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/plugin_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/plugin_config.h"
    @ONLY)

set(FTPSRV_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ftpsrv")
set(FTPSRV_SOURCES
    "${FTPSRV_SOURCE_DIR}/srv.c"
    "${FTPSRV_SOURCE_DIR}/cmd.c"
    "${FTPSRV_SOURCE_DIR}/cmd-proc.c"
    "${FTPSRV_SOURCE_DIR}/cmd-prospero.c"
    "${FTPSRV_SOURCE_DIR}/notify.c"
    "${FTPSRV_SOURCE_DIR}/io.c"
    "${FTPSRV_SOURCE_DIR}/io-freebsd.c"
    "${FTPSRV_SOURCE_DIR}/self.c"
    "${FTPSRV_SOURCE_DIR}/self-prospero.c"
    "${FTPSRV_SOURCE_DIR}/sha256.c")

eta_add_plugin(
    NAME ${ETA_PLUGIN_TARGET}
    TITLE_ID ${ETA_PLUGIN_ID}
    VERSION ${ETA_PLUGIN_VERSION}
    SOURCES
        source/main.c
        source/ftp_i18n.c
        source/ftp_service.c
        source/plugin_descriptor.c
        source/plugin_settings.c
        source/plugin_ui.c
        ${FTPSRV_SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${FTPSRV_SOURCE_DIR}"
    LIBRARIES
        SceLibcInternal
        SceSystemService
        SceNotification
        kernel_sys)

set_target_properties(${ETA_PLUGIN_TARGET} PROPERTIES
    OUTPUT_NAME "${ETA_PLUGIN_ID}.elf")
target_compile_definitions(${ETA_PLUGIN_TARGET} PRIVATE
    VERSION_TAG="etaHEN FTP plugin ${ETA_PLUGIN_VERSION}")
set_source_files_properties(${FTPSRV_SOURCES} PROPERTIES
    COMPILE_FLAGS
    "-Wno-error -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare")
