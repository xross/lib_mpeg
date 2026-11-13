set(LIB_NAME lib_mpeg)
set(LIB_VERSION 0.1.0)
set(LIB_DEPENDENT_MODULES "")
set(MPEG_BACKEND pl_mpeg CACHE STRING "Decoder backend: pl_mpeg (MIT) or libmpeg2 (GPL)")
set_property(CACHE MPEG_BACKEND PROPERTY STRINGS pl_mpeg libmpeg2)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/optimization.cmake)
# XCommon CMake applies the application's flags to library sources too. State
# dual issue explicitly so an application built with -mno-dual-issue does not
# silently make the decoder single issue; these options are appended last.
set(LIB_COMPILER_FLAGS ${MPEG_COLD_OPT} -g -fwrapv -mdual-issue)
set(LIB_ASM_SRCS "")
if(MPEG_BACKEND STREQUAL "pl_mpeg")
    set(MPEG_RGB565_VPU ON CACHE BOOL "Use the bit-exact XS3 VPU RGB565 converter")
    if(MPEG_RGB565_VPU)
        set(rgb565_vpu_flag -DMPEG_RGB565_VPU=1)
        set(LIB_ASM_SRCS src/rgb565/mpeg_rgb565_xs3.S)
    else()
        set(rgb565_vpu_flag -DMPEG_RGB565_VPU=0)
    endif()
    set(LIB_INCLUDES api src/pl_mpeg ../third_party/pl_mpeg)
    set(LIB_C_SRCS src/pl_mpeg/mpeg_video.c src/pl_mpeg/mpeg_memory.c src/rgb565/mpeg_rgb565.c)
    # This TU contains the entire single-header decoder, including IDCT and
    # motion compensation. Keep it hot; the static allocator uses the default.
    # Supported push input installs no callbacks, but upstream callback branches
    # still prevent complete automatic stack analysis.
    set(LIB_COMPILER_FLAGS_mpeg_video.c -O3 -g -fwrapv -mdual-issue -Wno-xcore-fptrgroup)
    # Per-file flags replace the module flags in XTC 15.3.1: carry the backend
    # selector here as well as optimisation flags, so OFF cannot fall back to
    # the source's VPU-on default.
    set(LIB_COMPILER_FLAGS_mpeg_rgb565.c -O3 -g -fwrapv -mdual-issue ${rgb565_vpu_flag})
elseif(MPEG_BACKEND STREQUAL "libmpeg2")
    set(LIB_INCLUDES api src/libmpeg2 ../third_party/libmpeg2/include ../third_party/libmpeg2/libmpeg2)
    list(APPEND LIB_COMPILER_FLAGS -Wno-xcore-fptrgroup)
    foreach(hot_file decode.c slice.c idct.c motion_comp.c)
        set(LIB_COMPILER_FLAGS_${hot_file} -O3 -g -fwrapv -mdual-issue -Wno-xcore-fptrgroup)
    endforeach()
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/upstream.cmake)
    mpeg2_prepare_sources(mpeg2_sources)
    set(LIB_C_SRCS "")
    foreach(source ${mpeg2_sources})
        file(RELATIVE_PATH relative_source "${CMAKE_CURRENT_LIST_DIR}" "${source}")
        list(APPEND LIB_C_SRCS "${relative_source}")
    endforeach()
else()
    message(FATAL_ERROR "Unknown MPEG_BACKEND: ${MPEG_BACKEND}")
endif()
set(LIB_XC_SRCS "")
set(LIB_CXX_SRCS "")
XMOS_REGISTER_MODULE()
