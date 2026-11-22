cmake_minimum_required(VERSION 3.20)

set(basename "eta_elfldr_server")
project(${basename} C)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
eta_output_dirs()

eta_module_prologue(C_STANDARD 17)

add_executable(${PROJECT_NAME} source/main.c)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "eta_elfldr.elf")

set(CMAKE_C_FLAGS "${ETA_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_POSIX_SOURCE -D_POSIX_C_SOURCE=200112 -D__BSD_VISIBLE=1 -D__XSI_VISIBLE=500")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -O2")

target_include_directories(${PROJECT_NAME} PRIVATE
  "${PROJECT_ROOT}/include"
  "${PROJECT_ROOT}/libeta_elfldr/include"
  "${PROJECT_ROOT}/libeta_platform/include"
)
target_link_directories(${PROJECT_NAME} PUBLIC "${ETAHEN_PS5_STUB_DIR}")
add_dependencies(${PROJECT_NAME} eta_elfldr)
target_link_libraries(${PROJECT_NAME} PUBLIC eta_platform eta_elfldr)

eta_strip_debug(${PROJECT_NAME})
