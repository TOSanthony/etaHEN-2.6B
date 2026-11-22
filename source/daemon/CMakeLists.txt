cmake_minimum_required (VERSION 3.20)

set(basename "daemon")
project(${basename} C CXX ASM)

eta_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
eta_output_dirs()
set(D_SRC ${D_CWD}/source)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")

# Finalize main target sources
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")

file(GLOB SrcFiles CONFIGURE_DEPENDS ${D_SRC}/*.c ${D_SRC}/*.cpp ${D_SRC}/../include/*.h ${D_SRC}/../include/*.hpp ${D_SRC}/*.s ${ETAHEN_THIRD_PARTY_DIR}/cjson/*.cpp ${ETAHEN_THIRD_PARTY_DIR}/cjson/*.hpp)

set(CMAKE_C_FLAGS "${ETA_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles} "${ETAHEN_COMMON_DIR}/elfldr_remote.c")
# .incbin in embedded_payloads.c is invisible to the compiler dependency scanner.
# Without OBJECT_DEPENDS, embedded payload rebuilds do not re-embed into daemon.elf.
if(DEFINED ETAHEN_OUT_BIN)
	set(_eta_util_elf "${ETAHEN_OUT_BIN}/util.elf")
	set(_eta_elfldr_elf "${ETAHEN_OUT_BIN}/eta_elfldr.elf")
else()
	set(_eta_util_elf "${PROJECT_ROOT}/bin/util.elf")
	set(_eta_elfldr_elf "${PROJECT_ROOT}/bin/eta_elfldr.elf")
endif()
set_source_files_properties(
	${D_SRC}/embedded_payloads.c
	PROPERTIES OBJECT_DEPENDS
		"${ETAHEN_OUT_BIN}/shellui.elf;${_eta_util_elf};${_eta_elfldr_elf}"
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
		ETAHEN_SHELLUI_ELF="${ETAHEN_OUT_BIN}/shellui.elf"
		ETAHEN_UTIL_ELF="${_eta_util_elf}"
		ETAHEN_ELFLDR_ELF="${_eta_elfldr_elf}"
)
eta_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
	"${D_CWD}/include"
	"${ETAHEN_THIRD_PARTY_DIR}/cjson"
	"${PROJECT_ROOT}/libeta_settings/include"
	"${PROJECT_ROOT}/libeta_ipc/include"
	"${PROJECT_ROOT}/libeta_proc/include"
	"${PROJECT_ROOT}/libeta_fps/include"
	"${PROJECT_ROOT}/libeta_lifecycle/include"
	"${PROJECT_ROOT}/libeta_sprx/include"
	"${PROJECT_ROOT}/libeta_platform/include"
	"${PROJECT_ROOT}/libeta_plugin_session/include"
	"${PROJECT_ROOT}/libeta_plugin_ui/include"
	"${PROJECT_ROOT}/libeta_plugin_bridge/include"
	"${PROJECT_ROOT}/libeta_plugin_manager/include"
	"${PROJECT_ROOT}/include"
)
target_link_directories	(${PROJECT_NAME} PUBLIC "${ETAHEN_PS5_STUB_DIR}")
add_dependencies(${PROJECT_NAME} eta_version util eta_elfldr_server NineS shellui hijacker eta_settings eta_ipc eta_proc eta_fps eta_platform eta_ready eta_lifecycle eta_plugin_session eta_plugin_bridge eta_plugin_manager eta_elfldr eta_sprx)
# Supervises private :9020 via external :9021, then restarts embedded util via :9020.
target_link_libraries	(${PROJECT_NAME} PUBLIC eta_ipc eta_settings eta_proc eta_fps eta_platform eta_ready eta_lifecycle eta_plugin_session eta_plugin_bridge eta_plugin_manager eta_elfldr eta_sprx hijacker SceSystemService SceNotification SceUserService SceNetCtl SceSysCore kernel_sys NineS)

eta_strip_debug(${PROJECT_NAME})
