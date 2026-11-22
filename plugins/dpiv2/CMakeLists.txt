cmake_minimum_required(VERSION 3.20)

project(etaHENDpiV2Plugin VERSION 1.0.0 LANGUAGES C)

set(ETA_PLUGIN_TARGET dpiv2)
set(ETA_PLUGIN_ID DPIV00001)
set(ETA_PLUGIN_VERSION 1.00)
set(ETA_PLUGIN_NAME "DPI v2")

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

find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB DPI_LOCALE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/*.json")
set(DPI_I18N_CATALOG
    "${CMAKE_CURRENT_BINARY_DIR}/generated/dpi_i18n_catalog.inc")
add_custom_command(
    OUTPUT "${DPI_I18N_CATALOG}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_notification_catalog.py"
        --locales-dir "${CMAKE_CURRENT_SOURCE_DIR}/i18n"
        --output "${DPI_I18N_CATALOG}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_notification_catalog.py"
        ${DPI_LOCALE_FILES}
    VERBATIM)

set(PKGSERVER_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pkgserver")
set(PKGSERVER_SOURCES
    "${PKGSERVER_SOURCE_DIR}/pkg_server.c"
    "${PKGSERVER_SOURCE_DIR}/multipart.c")
set(WEBUI_INDEX "${CMAKE_CURRENT_SOURCE_DIR}/webui/dist/index.html")
if(NOT EXISTS "${WEBUI_INDEX}")
    message(FATAL_ERROR
        "DPI WebUI bundle missing: ${WEBUI_INDEX}. Run npm run build in webui/.")
endif()

eta_add_plugin(
    NAME ${ETA_PLUGIN_TARGET}
    TITLE_ID ${ETA_PLUGIN_ID}
    VERSION ${ETA_PLUGIN_VERSION}
    SOURCES
        source/main.c
        source/dpi_i18n.c
        source/dpi_service.c
        source/plugin_descriptor.c
        source/plugin_settings.c
        source/plugin_ui.c
        ${PKGSERVER_SOURCES}
        "${DPI_I18N_CATALOG}"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${PKGSERVER_SOURCE_DIR}"
        "${PKGSERVER_SOURCE_DIR}/include"
    LIBRARIES
        SceLibcInternal
        SceSystemService
        SceAppInstUtil
        SceNotification
        SceNetCtl
        SceNet
        kernel_sys)

set_source_files_properties("${PKGSERVER_SOURCE_DIR}/pkg_server.c" PROPERTIES
    COMPILE_DEFINITIONS "ETAHEN_WEBUI_INDEX=\"${WEBUI_INDEX}\""
    OBJECT_DEPENDS "${WEBUI_INDEX}")
set_source_files_properties(${PKGSERVER_SOURCES} PROPERTIES
    COMPILE_FLAGS
    "-Wno-error -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare")
