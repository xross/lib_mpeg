# Copyright 2026 XMOS LIMITED.
# SPDX-License-Identifier: MIT
# Keep the pinned upstream checkout pristine. The sole source transformation is
# applied to a build-directory copy, with an exact-match guard against drift.
function(mpeg2_prepare_sources out_sources)
    set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(upstream "${root}/third_party/libmpeg2")
    if(NOT EXISTS "${upstream}/libmpeg2/decode.c")
        message(FATAL_ERROR "Pinned third_party/libmpeg2 dependency is missing")
    endif()
    set(MPEG2_CHUNK_BYTES 32768 CACHE STRING "Maximum compressed MPEG slice/header chunk")
    if(NOT MPEG2_CHUNK_BYTES MATCHES "^[0-9]+$" OR
       MPEG2_CHUNK_BYTES LESS 1024 OR MPEG2_CHUNK_BYTES GREATER 1222656)
        message(FATAL_ERROR "MPEG2_CHUNK_BYTES must be between 1024 and 1222656")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${upstream}/libmpeg2/decode.c")
    file(READ "${upstream}/libmpeg2/decode.c" decode)
    set(original "#define BUFFER_SIZE (1194 * 1024)")
    string(FIND "${decode}" "${original}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Upstream decode.c changed; review chunk-buffer adaptation")
    endif()
    string(REPLACE "${original}"
        "/* xcore evaluation change, 2026-11-12: bound compressed chunks. */\n#define BUFFER_SIZE ${MPEG2_CHUNK_BYTES}"
        decode "${decode}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/mpeg2-generated")
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/mpeg2-generated/decode.c"
        CONTENT "${decode}" @ONLY)
    set(sources "${CMAKE_BINARY_DIR}/mpeg2-generated/decode.c")
    foreach(name alloc header slice motion_comp idct cpu_accel cpu_state)
        list(APPEND sources "${upstream}/libmpeg2/${name}.c")
    endforeach()
    set(${out_sources} "${sources}" PARENT_SCOPE)
endfunction()
