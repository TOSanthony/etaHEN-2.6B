cmake_minimum_required (VERSION 3.20)

set(basename "util")

project(${basename} C CXX ASM)

eta_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
eta_output_dirs()
set(D_SRC ${D_CWD}/source)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "util.elf")

target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)

file(GLOB SrcFiles CONFIGURE_DEPENDS
  ${D_SRC}/*.c
  ${D_SRC}/*.cpp
  ${D_SRC}/cheats/*.c
  ${D_SRC}/cheats/*.cpp
  ${D_SRC}/cheats/sync/*.cpp
  ${ETAHEN_THIRD_PARTY_DIR}/cheat_support/*.c
  ${D_SRC}/../include/*.h
  ${D_SRC}/../include/*.hpp
  ${D_SRC}/*.s
  ${ETAHEN_THIRD_PARTY_DIR}/cjson/*.cpp
  ${ETAHEN_THIRD_PARTY_DIR}/cjson/*.hpp
 
)

set(CMAKE_C_FLAGS "${ETA_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -Wno-unused-const-variable -Wno-deprecated-declarations -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles}
  "${ETAHEN_COMMON_DIR}/elfldr_remote.c")
eta_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
  "${D_CWD}/include"
  "${ETAHEN_THIRD_PARTY_DIR}/cjson"
  # miniz / sha256 / mc4 headers live beside their vendored .c files.
  "${ETAHEN_THIRD_PARTY_DIR}/cheat_support"
  "${ETAHEN_THIRD_PARTY_DIR}/keystone/include"
  "${PROJECT_ROOT}/libhijacker/include"
  "${PROJECT_ROOT}/libeta_settings/include"
  "${PROJECT_ROOT}/libeta_ipc/include"
  "${PROJECT_ROOT}/libeta_proc/include"
  "${PROJECT_ROOT}/libeta_platform/include"
  "${PROJECT_ROOT}/libeta_payload/include"
  "${PROJECT_ROOT}/libeta_elfldr/include"
  "${PROJECT_ROOT}/include"
)
# CBC for MC4 / ShnExt AES
target_compile_definitions(${PROJECT_NAME} PRIVATE CBC=1)
set(ETA_CURL_ROOT "${PS5_PAYLOAD_SDK}/target/user/homebrew")
set(ETA_CURL_CA_BUNDLE "${ETA_CURL_ROOT}/etc/ca-bundle.crt")
if(NOT EXISTS "${ETA_CURL_ROOT}/include/curl/curl.h" OR
   NOT EXISTS "${ETA_CURL_ROOT}/lib/libcurl.a" OR
   NOT EXISTS "${ETA_CURL_CA_BUNDLE}")
	message(FATAL_ERROR
		"PS5 payload SDK libcurl and CA bundle are required for cheat catalog HTTP "
		"(looked in ${ETA_CURL_ROOT}).")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
	CURL_STATICLIB
	ETA_CURL_CA_BUNDLE="${ETA_CURL_CA_BUNDLE}"
)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
	"${ETA_CURL_ROOT}/include")
# The CA bundle is embedded with .incbin, which is invisible to the compiler's
# normal dependency scanner.
set_source_files_properties(
	"${D_SRC}/cheats/sync/http_transport_ps5.cpp"
	PROPERTIES OBJECT_DEPENDS "${ETA_CURL_CA_BUNDLE}"
)
target_link_directories(${PROJECT_NAME} PUBLIC
  "${ETAHEN_PS5_STUB_DIR}"
  "${ETAHEN_THIRD_PARTY_DIR}/keystone/lib"
  "${ETA_CURL_ROOT}/lib"
  "${PS5_PAYLOAD_SDK}/target/lib"
)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")
add_dependencies(${PROJECT_NAME} hijacker eta_settings eta_ipc eta_proc eta_platform eta_payload eta_ready eta_elfldr)
# keystone: ShnExt ASM only
target_link_libraries(${PROJECT_NAME} PUBLIC
    eta_ipc
    eta_settings
    eta_proc
    eta_platform
    eta_payload
    eta_ready
    eta_elfldr
    keystone
    hijacker
    SceSystemService
    SceUserService
    SceNotification
    SceNetCtl
    SceNet
    kernel_sys
    curl
    psl
    ssl
    crypto
    zstd
    z
)
set_source_files_properties(
  ${ETAHEN_THIRD_PARTY_DIR}/cheat_support/aes.c
  ${ETAHEN_THIRD_PARTY_DIR}/cheat_support/base64.c
  ${ETAHEN_THIRD_PARTY_DIR}/cheat_support/miniz.c
  ${ETAHEN_THIRD_PARTY_DIR}/cheat_support/sha256.c
  PROPERTIES COMPILE_FLAGS "-Wno-error -Wno-unused-function -Wno-sign-compare -Wno-unused-parameter"
)
# miniz.h ships static-inline helpers that trip PS5's global -Werror in C++.
set_source_files_properties(
  ${D_SRC}/cheats/sync/zip_archive.cpp
  PROPERTIES COMPILE_FLAGS "-Wno-error -Wno-unused-function -Wno-sign-compare -Wno-unused-parameter"
)
file(GLOB CHEAT_ENGINE_SRCS CONFIGURE_DEPENDS ${D_SRC}/cheats/*.c)
set_source_files_properties(${CHEAT_ENGINE_SRCS} PROPERTIES
  COMPILE_FLAGS "-Wno-error -Wno-unused-parameter -Wno-sign-compare"
)

eta_strip_debug(${PROJECT_NAME})
