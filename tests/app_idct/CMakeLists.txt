cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(test_mpeg_idct)
set(APP_HW_TARGET XK-EVK-XU316)
set(APP_C_SRCS ../../third_party/pl_mpeg/tests/idct_test.c)
set(APP_ASM_SRCS ../../third_party/pl_mpeg/xcore/pl_mpeg_idct_xs3.S)
set(APP_INCLUDES ../../third_party/pl_mpeg)
# vpu16 checks the vector transform against its scalar model and times it;
# plmpeg times the original transform on the same coefficients.
set(idct_flags -O3 -g -report -mcmodel=large -mdual-issue -fwrapv -Wno-xcore-fptrgroup)
set(APP_COMPILER_FLAGS_vpu16 ${idct_flags} -DPLM_VIDEO_IDCT=2)
set(APP_COMPILER_FLAGS_plmpeg ${idct_flags} -DPLM_VIDEO_IDCT=0)
# Finite standalone test: JTAG logging only, outside the timed sections.
XMOS_REGISTER_APP()
foreach(target ${APP_BUILD_TARGETS})
    string(REPLACE "test_mpeg_idct_" "" configuration "${target}")
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${configuration}")
endforeach()
