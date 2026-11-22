cmake_minimum_required(VERSION 3.20)
set(basename "eta_fps")
project(${basename} C CXX)

eta_module_prologue(C_STANDARD 11 CXX_STANDARD 20)
set(D_CWD "${PROJECT_SOURCE_DIR}")

add_library(${PROJECT_NAME} STATIC
  source/fps_formula.cpp
  source/fps_dce.cpp
  source/fps_agc.cpp
  source/fps_publish.cpp
  source/fps_bc.cpp
  ${PROJECT_ROOT}/libeta_detour/source/hde64.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_C_FLAGS "${ETA_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -Werror -fno-exceptions")

target_include_directories(${PROJECT_NAME}
  PUBLIC  "${D_CWD}/include"
  PRIVATE "${PROJECT_ROOT}/include"
  PRIVATE "${PROJECT_ROOT}/libeta_proc/include"
  PRIVATE "${PROJECT_ROOT}/libeta_platform/include"
  PRIVATE "${PROJECT_ROOT}/libhijacker/include"
  PRIVATE "${PROJECT_ROOT}/libeta_detour/include"
)

add_dependencies(${PROJECT_NAME} eta_proc eta_platform hijacker)
