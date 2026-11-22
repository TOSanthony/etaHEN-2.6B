cmake_minimum_required (VERSION 3.20)

project("etaHEN")

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EtaTarget)
get_filename_component(ETAHEN_ROOT "${PROJECT_ROOT}/.." ABSOLUTE)
set(ETAHEN_THIRD_PARTY_DIR "${ETAHEN_ROOT}/third_party" CACHE PATH
	"Third-party dependency source directory")
set(ETAHEN_COMMON_DIR "${PROJECT_ROOT}/common")
set(ETAHEN_PS5_STUB_DIR "${PROJECT_ROOT}/platform/ps5/stubs")
set(ETAHEN_KSTUFF_ELF "${ETAHEN_ROOT}/.cache/dependencies/kstuff.elf"
	CACHE FILEPATH "Path to the kstuff ELF embedded by the bootstrapper")
set(ETAHEN_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
set(ETAHEN_VERSION_HEADER
	"${ETAHEN_GENERATED_INCLUDE_DIR}/eta/version.h")

# Re-evaluate Git state on every build. The script preserves the header mtime
# when the version is unchanged, so incremental compilation remains cheap.
add_custom_target(eta_version
	COMMAND "${CMAKE_COMMAND}"
		"-DREPO_ROOT=${ETAHEN_ROOT}"
		"-DOUTPUT_FILE=${ETAHEN_VERSION_HEADER}"
		-P "${PROJECT_ROOT}/cmake/GenerateVersionHeader.cmake"
	BYPRODUCTS "${ETAHEN_VERSION_HEADER}"
	COMMENT "Generating etaHEN build version"
	VERBATIM
)

# ---------------------------------------------------------------------------
# Artifact layout
#   ${CMAKE_BINARY_DIR}/bin  — ELFs (daemon, util, bootstrapper, etaHEN, …)
#   ${CMAKE_BINARY_DIR}/lib  — built static libs (libeta_*, …)
# PS5 system-library stubs stay under source/platform/ps5/stubs.
#
# Default scripts/build.sh uses -B <repo>/build so products land at repo/build/.
# ---------------------------------------------------------------------------
set(ETAHEN_OUT_BIN "${CMAKE_BINARY_DIR}/bin" CACHE PATH "ELF output directory")
set(ETAHEN_OUT_LIB "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Built static-lib output directory")

file(MAKE_DIRECTORY "${ETAHEN_OUT_BIN}" "${ETAHEN_OUT_LIB}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ETAHEN_OUT_BIN})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ETAHEN_OUT_LIB})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${ETAHEN_OUT_LIB})

include_directories(BEFORE "${ETAHEN_GENERATED_INCLUDE_DIR}")
# First-party shared contracts — not SYSTEM, so warnings in them are visible.
include_directories("${PROJECT_ROOT}/include")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}/include")

# Built libraries first, then PS5 system stubs.
link_directories("${ETAHEN_OUT_LIB}" "${ETAHEN_PS5_STUB_DIR}")

add_subdirectory(libhijacker)
add_subdirectory(libNidResolver)
# add_subdirectory(libRPI)
# libSelfDecryptor removed from etaHEN.
# Shared proc/ucred before NineS (inject path links these symbols).
add_subdirectory(libeta_proc)
add_subdirectory(libeta_fps)
add_subdirectory(libeta_platform)
add_subdirectory(libeta_payload)
add_subdirectory(libeta_conflict)
add_subdirectory(libeta_ipc)
add_subdirectory(libeta_settings)
add_subdirectory(libeta_detour)
add_subdirectory(libeta_ready)
add_subdirectory(libeta_lifecycle)
add_subdirectory(libeta_plugin_ui)
add_subdirectory(libeta_plugin_bridge)
add_subdirectory(libeta_plugin_session)
add_subdirectory(libeta_plugin_manager)
# Shared ptrace + ELF inject (authid elevated once at inject entry, not per syscall).
add_subdirectory(libeta_elfldr)
add_subdirectory(libeta_sprx)
add_subdirectory(eta_elfldr_server)
add_subdirectory(libNineS)
add_subdirectory(shellui)
add_subdirectory(util)
add_subdirectory(daemon)
add_subdirectory(bootstrapper)
add_subdirectory(unpacker)
