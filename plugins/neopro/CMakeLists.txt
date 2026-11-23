cmake_minimum_required(VERSION 3.20)

project(etaHENPluginNeoPro VERSION 1.0.0 LANGUAGES C CXX ASM)

# Plugin identity. Keep these values stable after publishing a plugin.
set(ETA_PLUGIN_TARGET neopro)
set(ETA_PLUGIN_ID NEOP00001)
set(ETA_PLUGIN_VERSION 1.00)
set(ETA_PLUGIN_NAME "PS4 Pro Mode")

if(NOT ETA_PLUGIN_ID MATCHES "^[A-Za-z][A-Za-z][A-Za-z][A-Za-z][0-9][0-9][0-9][0-9][0-9]$")
    message(FATAL_ERROR "ETA_PLUGIN_ID must contain four letters followed by five digits")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

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

# ---------------------------------------------------------------------------
# NeoEnabler launch shim: position independent code assembled for the host
# (x86-64, no OS) and turned into shim_data.h by NeoEnabler's gen_shim.py.
# ---------------------------------------------------------------------------
set(NEO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/neo")
set(SHIM_BUILD "${CMAKE_CURRENT_BINARY_DIR}/shim")
set(SHIM_CLANG "${PS5_PAYLOAD_SDK}/bin/clang")
set(SHIM_LLD "${PS5_PAYLOAD_SDK}/bin/ld.lld")
set(SHIM_OBJCOPY "${PS5_PAYLOAD_SDK}/bin/llvm-objcopy")
set(SHIM_NM "${PS5_PAYLOAD_SDK}/bin/llvm-nm")
add_custom_command(
    OUTPUT "${SHIM_BUILD}/shim_data.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHIM_BUILD}"
    COMMAND "${SHIM_CLANG}" --target=x86_64-unknown-none -c "${NEO_DIR}/shim.S" -o "${SHIM_BUILD}/shim.o"
    COMMAND "${SHIM_LLD}" -T "${NEO_DIR}/shim.ld" --no-relax "${SHIM_BUILD}/shim.o" -o "${SHIM_BUILD}/shim.out"
    COMMAND "${SHIM_OBJCOPY}" -O binary -j .shim "${SHIM_BUILD}/shim.out" "${SHIM_BUILD}/shim.bin"
    COMMAND "${Python3_EXECUTABLE}" "${NEO_DIR}/tools/gen_shim.py" "${SHIM_NM}" "${SHIM_BUILD}"
    DEPENDS "${NEO_DIR}/shim.S" "${NEO_DIR}/shim.ld" "${NEO_DIR}/tools/gen_shim.py"
    COMMENT "Generating the NeoEnabler launch shim"
    VERBATIM)
# 8 GiB budget barrier shim (BeginAppMount2), same toolchain.
add_custom_command(
    OUTPUT "${SHIM_BUILD}/step_shim_data.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHIM_BUILD}"
    COMMAND "${SHIM_CLANG}" --target=x86_64-unknown-none -c "${NEO_DIR}/step_shim.S" -o "${SHIM_BUILD}/step-shim.o"
    COMMAND "${SHIM_LLD}" -T "${NEO_DIR}/step_shim.ld" --no-relax "${SHIM_BUILD}/step-shim.o" -o "${SHIM_BUILD}/step-shim.out"
    COMMAND "${SHIM_OBJCOPY}" -O binary -j .shim "${SHIM_BUILD}/step-shim.out" "${SHIM_BUILD}/step-shim.bin"
    COMMAND "${Python3_EXECUTABLE}" "${NEO_DIR}/tools/gen_step_shim.py" "${SHIM_NM}" "${SHIM_BUILD}"
    DEPENDS "${NEO_DIR}/step_shim.S" "${NEO_DIR}/step_shim.ld" "${NEO_DIR}/tools/gen_step_shim.py"
    COMMENT "Generating the NeoEnabler 8 GiB step shim"
    VERBATIM)
add_custom_target(neopro_shim DEPENDS "${SHIM_BUILD}/shim_data.h" "${SHIM_BUILD}/step_shim_data.h")

# ---------------------------------------------------------------------------
# Game patches (NeoEnabler xml/) embedded in the ELF with .incbin.
# ---------------------------------------------------------------------------
file(GLOB NEO_XML_FILES "${CMAKE_CURRENT_SOURCE_DIR}/xml/*.xml")
list(SORT NEO_XML_FILES)
set(XML_BUNDLE_C "${CMAKE_CURRENT_BINARY_DIR}/generated/xml_bundle.c")
set(_c "#include <stddef.h>\n#include \"xml_bundle.h\"\n")
set(_table "")
set(_i 0)
foreach(_f ${NEO_XML_FILES})
    get_filename_component(_n "${_f}" NAME)
    string(APPEND _c "__asm__(\".section .rodata\\n.balign 16\\nneo_xml_${_i}_begin:\\n.incbin \\\"${_f}\\\"\\nneo_xml_${_i}_end:\\n.byte 0\\n.text\");\nextern const char neo_xml_${_i}_begin[], neo_xml_${_i}_end[];\n")
    string(APPEND _table "    {\"${_n}\", neo_xml_${_i}_begin, neo_xml_${_i}_end},\n")
    math(EXPR _i "${_i}+1")
endforeach()
string(APPEND _c "const neo_xml_entry neo_xml_bundle[] = {\n${_table}    {NULL, NULL, NULL}\n};\nconst unsigned neo_xml_bundle_count = ${_i};\n")
file(GENERATE OUTPUT "${XML_BUNDLE_C}" CONTENT "${_c}")
set_source_files_properties("${XML_BUNDLE_C}" PROPERTIES GENERATED TRUE
    OBJECT_DEPENDS "${NEO_XML_FILES}")

set(PSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pspatch")
set(PSP_C_SOURCES
    ${PSP_DIR}/shared/app_metadata.c ${PSP_DIR}/shared/dynlib.c ${PSP_DIR}/shared/file.c
    ${PSP_DIR}/shared/match.c ${PSP_DIR}/shared/memory.c ${PSP_DIR}/shared/notify.c
    ${PSP_DIR}/shared/patch-common.c ${PSP_DIR}/shared/proc.c ${PSP_DIR}/shared/proc_rw.c
    ${PSP_DIR}/shared/shellcode_symbol.c ${PSP_DIR}/shared/sigqueue.c ${PSP_DIR}/shared/sys_state.c
    ${PSP_DIR}/patch/patch_shellcore.c)
set(PSP_CXX_SOURCES
    ${PSP_DIR}/shared/memory.cpp ${PSP_DIR}/shared/my_assert.cpp
    ${PSP_DIR}/patch-server/patch_reg.cpp ${PSP_DIR}/patch-server/patch_signal.cpp
    ${PSP_DIR}/patch-server/patch_user_xml.cpp ${PSP_DIR}/patch-server/patch_web_xml.cpp
    ${PSP_DIR}/patch-server/server_120hz_patch.cpp ${PSP_DIR}/patch-server/server_patch.cpp
    ${PSP_DIR}/patch-server/server_signal.cpp ${PSP_DIR}/patch-server/server_thread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugixml/pugixml.cpp
    source/pspatch_glue.cpp)

eta_add_plugin(
    NAME ${ETA_PLUGIN_TARGET}
    TITLE_ID ${ETA_PLUGIN_ID}
    VERSION ${ETA_PLUGIN_VERSION}
    SOURCES
        source/main.c
        source/neo_core.c
        source/plugin_descriptor.c
        source/plugin_settings.c
        source/plugin_ui.c
        "${XML_BUNDLE_C}"
        ${PSP_C_SOURCES}
        ${PSP_CXX_SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/neo"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    LIBRARIES
        SceLibcInternal
        SceSystemService
        SceNotification
        SceUserService
        kernel_sys)

add_dependencies(${ETA_PLUGIN_TARGET} neopro_shim)

# Vendored code (ps-patch-system, pugixml) keeps its own warning level and C++ dialect.
set_source_files_properties(${PSP_C_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-w;-std=gnu2x;-fms-extensions;-DELF_NAME=\"NEOP00001.elf\"")
set_source_files_properties(${PSP_CXX_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-w;-std=gnu++2b;-fms-extensions;-DELF_NAME=\"NEOP00001.elf\"")
set_source_files_properties(source/neo_core.c PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-function;-Wno-unused-parameter;-Wno-unused-variable;-Wno-sign-compare;-Wno-missing-field-initializers")
target_include_directories(${ETA_PLUGIN_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugixml"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pspatch"
    "${SHIM_BUILD}")
set_target_properties(${ETA_PLUGIN_TARGET} PROPERTIES
    OUTPUT_NAME "${ETA_PLUGIN_ID}.elf")
