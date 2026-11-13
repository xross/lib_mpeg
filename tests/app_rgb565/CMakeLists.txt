cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(test_mpeg_rgb565)
set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(APP_HW_TARGET XK-EVK-XU316 CACHE STRING "XU316 target for the RGB565 test")
set(APP_C_SRCS ../rgb565_test.c)
set(APP_INCLUDES ../../third_party/pl_mpeg_upstream)
set(APP_COMPILER_FLAGS -O3 -g -report -mcmodel=large -mdual-issue -fwrapv
    -Wno-xcore-fptrgroup)
# This finite, standalone test uses JTAG I/O outside its timed sections; it has
# no USB/I/O real-time tasks and does not need board-specific xSCOPE wiring.
include(${CMAKE_CURRENT_LIST_DIR}/../../examples/deps.cmake)
XMOS_REGISTER_APP()
# Keep the independent upstream colour oracle in the test, but not the decoder
# TU: both instantiate PL_MPEG's single header and export the same symbols.
foreach(target ${APP_BUILD_TARGETS})
    get_target_property(sources ${target} SOURCES)
    list(FILTER sources EXCLUDE REGEX "/lib_mpeg/src/pl_mpeg/")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
endforeach()
set_target_properties(${APP_BUILD_TARGETS} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
