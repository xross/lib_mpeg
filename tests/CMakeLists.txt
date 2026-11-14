cmake_minimum_required(VERSION 3.21)
project(mpeg_evaluation C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Host build of the decoder and its tests. xcore applications use
# lib_mpeg/lib_build_info.cmake through XCommon CMake, not this file.
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MPEG_BACKEND pl_mpeg CACHE STRING "Decoder backend: pl_mpeg (MIT) or libmpeg2 (GPL)")
set_property(CACHE MPEG_BACKEND PROPERTY STRINGS pl_mpeg libmpeg2)
include(${root}/cmake/optimization.cmake)
option(MPEG_SANITIZE "Enable native address/undefined sanitizers" ON)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(MPEG_BACKEND STREQUAL "pl_mpeg")
    option(MPEG_TEST_ORIGINAL_IDCT "Test the original transform instead of the default scalar model" OFF)
    set(MPEG_TEST_VIDEO_STREAM "" CACHE FILEPATH "Optional full MPEG-1 elementary video accuracy fixture")
    set(MPEG_TEST_AUDIO_STREAM "" CACHE FILEPATH "Optional full MP2 elementary audio precision fixture")
    add_subdirectory("${root}/third_party/pl_mpeg/tests" plmpeg-kernel-tests)
    foreach(target plmpeg_idct_test plmpeg_window_test)
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endforeach()
    add_library(mpeg_scalar STATIC ${root}/lib_mpeg/src/pl_mpeg/mpeg_video.c ${root}/lib_mpeg/src/pl_mpeg/mpeg_memory.c
        ${root}/lib_mpeg/src/rgb565/mpeg_rgb565.c)
    target_include_directories(mpeg_scalar PUBLIC ${root}/lib_mpeg/api PRIVATE ${root}/lib_mpeg/src/pl_mpeg
        ${root}/third_party/pl_mpeg)
    if(MPEG_TEST_ORIGINAL_IDCT)
        target_compile_definitions(mpeg_scalar PRIVATE MPEG_VIDEO_IDCT=MPEG_VIDEO_IDCT_PLMPEG)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(mpeg_scalar PUBLIC m)
    endif()
    target_compile_options(mpeg_scalar PRIVATE ${MPEG_COLD_OPT} -fwrapv)
    set_source_files_properties(${root}/lib_mpeg/src/pl_mpeg/mpeg_video.c ${root}/lib_mpeg/src/rgb565/mpeg_rgb565.c
        PROPERTIES COMPILE_OPTIONS -O3)
    # Native and xcore front ends reuse the owning example's benchmark core.
    add_executable(mpeg_bench ${root}/tools/mpeg_bench_main.c ${root}/examples/app_plmpeg_benchmark/src/plmpeg_benchmark.c)
    target_include_directories(mpeg_bench PRIVATE ${root}/examples/app_plmpeg_benchmark/src)
    target_compile_definitions(mpeg_bench PRIVATE _POSIX_C_SOURCE=200809L)
    add_executable(mpeg_api_test mpeg_api_test.c)
    add_executable(mpeg_motion_test motion_test.c)
    target_include_directories(mpeg_motion_test PRIVATE ${root}/third_party/pl_mpeg)
    target_compile_options(mpeg_motion_test PRIVATE -O3 -fwrapv)
    add_executable(mpeg_motion_big_endian_test motion_test.c)
    target_include_directories(mpeg_motion_big_endian_test PRIVATE ${root}/third_party/pl_mpeg)
    target_compile_definitions(mpeg_motion_big_endian_test PRIVATE MPEG_TEST_BIG_ENDIAN=1)
    target_compile_options(mpeg_motion_big_endian_test PRIVATE -O3 -fwrapv)
    # Desktop-only oracle, independent of our bounded allocator/feed adapter.
    # It is built from the untouched upstream checkout, never from the fork in
    # third_party/pl_mpeg, so the exact-output test cannot compare the fork
    # with itself.
    add_executable(plmpeg_upstream_ref plmpeg_upstream_ref.c)
    target_include_directories(plmpeg_upstream_ref PRIVATE ${root}/third_party/pl_mpeg_upstream)
    target_compile_options(plmpeg_upstream_ref PRIVATE -O3 -fwrapv)
    foreach(target mpeg_bench mpeg_api_test)
        target_compile_options(${target} PRIVATE ${MPEG_COLD_OPT} -Wall -Wextra -Werror)
        target_link_libraries(${target} PRIVATE mpeg_scalar)
    endforeach()
    # RGB565 conversion checked against unmodified upstream's RGB conversion.
    add_executable(mpeg_rgb565_test rgb565_test.c)
    target_include_directories(mpeg_rgb565_test PRIVATE ${root}/third_party/pl_mpeg_upstream)
    target_compile_options(mpeg_rgb565_test PRIVATE -O1 -fwrapv)
    target_link_libraries(mpeg_rgb565_test PRIVATE mpeg_scalar)
    set(decoder_target mpeg_scalar)
    set(test_targets mpeg_bench mpeg_api_test plmpeg_upstream_ref mpeg_rgb565_test mpeg_motion_test mpeg_motion_big_endian_test)
    add_test(NAME mpeg_api COMMAND mpeg_api_test)
    add_test(NAME mpeg_motion COMMAND mpeg_motion_test)
    add_test(NAME mpeg_motion_big_endian COMMAND mpeg_motion_big_endian_test)
    add_test(NAME mpeg_rgb565 COMMAND mpeg_rgb565_test)
    add_executable(mpeg_rgb565_vpu_test rgb565_test.c rgb565_vpu_model.c
        ${root}/lib_mpeg/src/rgb565/mpeg_rgb565.c)
    target_include_directories(mpeg_rgb565_vpu_test PRIVATE ${root}/lib_mpeg/api
        ${root}/lib_mpeg/src/rgb565 ${root}/third_party/pl_mpeg_upstream)
    target_compile_definitions(mpeg_rgb565_vpu_test PRIVATE MPEG_RGB565_VPU=1 MPEG_RGB565_VPU_EMULATE=1)
    target_compile_options(mpeg_rgb565_vpu_test PRIVATE -O3 -fwrapv)
    list(APPEND test_targets mpeg_rgb565_vpu_test)
    add_test(NAME mpeg_rgb565_vpu COMMAND mpeg_rgb565_vpu_test)
    # Only the original transform promises upstream's bytes. The default host
    # transform is the scalar model of the XS3 one, checked by quality bounds.
    set(plmpeg_reference_args "")
    if(NOT MPEG_TEST_ORIGINAL_IDCT)
        list(APPEND plmpeg_reference_args --approx-idct)
    endif()
    add_test(NAME mpeg1_reference COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_plmpeg.py --bench $<TARGET_FILE:mpeg_bench>
        --api-test $<TARGET_FILE:mpeg_api_test> --upstream $<TARGET_FILE:plmpeg_upstream_ref>
        --work ${CMAKE_CURRENT_BINARY_DIR}/fixtures ${plmpeg_reference_args})
    set_tests_properties(mpeg_api mpeg_motion mpeg_motion_big_endian mpeg_rgb565 mpeg_rgb565_vpu mpeg1_reference PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1" TIMEOUT 120)

    add_executable(yuv_compare yuv_compare.c)
    if(UNIX AND NOT APPLE)
        target_link_libraries(yuv_compare PRIVATE m)
    endif()
    list(APPEND test_targets yuv_compare)
    set(video_quality_args "")
    if(NOT MPEG_TEST_VIDEO_STREAM STREQUAL "")
        list(APPEND video_quality_args --full "${MPEG_TEST_VIDEO_STREAM}")
    endif()
    add_test(NAME mpeg_video_quality COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_video_quality.py --build ${CMAKE_CURRENT_BINARY_DIR}
        --work ${CMAKE_CURRENT_BINARY_DIR}/quality-fixtures ${video_quality_args})

    # Separate arithmetic variants are deliberate: the baseline must match
    # pristine upstream, while the fast paths have a bounded PCM error.
    foreach(variant "mp2_bench;0;0" "mp2_bench_fast;1;0" "mp2_bench_window;1;1")
        list(GET variant 0 target)
        list(GET variant 1 fast_scale)
        list(GET variant 2 fast_window)
        add_executable(${target} audio_bench.c ${root}/lib_mpeg/src/pl_mpeg/mpeg_video.c
            ${root}/lib_mpeg/src/pl_mpeg/mpeg_memory.c)
        target_include_directories(${target} PRIVATE ${root}/lib_mpeg/api ${root}/lib_mpeg/src/pl_mpeg
            ${root}/third_party/pl_mpeg)
        target_compile_definitions(${target} PRIVATE MPEG_AUDIO_FAST_SCALE=${fast_scale}
            MPEG_AUDIO_FAST_WINDOW=${fast_window})
        target_compile_options(${target} PRIVATE -O3 -fwrapv -ffp-contract=off -Wall -Wextra)
        if(UNIX AND NOT APPLE)
            target_link_libraries(${target} PRIVATE m)
        endif()
        list(APPEND test_targets ${target})
    endforeach()
    add_executable(mp2_upstream plmpeg_audio_ref.c)
    target_include_directories(mp2_upstream PRIVATE ${root}/third_party/pl_mpeg_upstream)
    target_compile_options(mp2_upstream PRIVATE -O3 -fwrapv -ffp-contract=off)
    if(UNIX AND NOT APPLE)
        target_link_libraries(mp2_upstream PRIVATE m)
    endif()
    list(APPEND test_targets mp2_upstream)
    set(audio_reference_args "")
    if(NOT MPEG_TEST_AUDIO_STREAM STREQUAL "")
        list(APPEND audio_reference_args --full "${MPEG_TEST_AUDIO_STREAM}")
    endif()
    add_test(NAME mpeg_audio_reference COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_audio.py --build ${CMAKE_CURRENT_BINARY_DIR}
        --work ${CMAKE_CURRENT_BINARY_DIR}/audio-fixtures ${audio_reference_args})
    add_test(NAME player_report_parser COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb_lcd/test_run_hardware.py)
    set_tests_properties(mpeg_video_quality mpeg_audio_reference player_report_parser PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1" TIMEOUT 240)
elseif(MPEG_BACKEND STREQUAL "libmpeg2")
    include(${root}/cmake/upstream.cmake)
    mpeg2_prepare_sources(mpeg2_sources)
    add_library(mpeg2_scalar STATIC ${mpeg2_sources})
    target_include_directories(mpeg2_scalar PUBLIC ${root}/lib_mpeg/api PRIVATE
        ${root}/lib_mpeg/src/libmpeg2 ${root}/third_party/libmpeg2/include ${root}/third_party/libmpeg2/libmpeg2)
    target_compile_options(mpeg2_scalar PRIVATE ${MPEG_COLD_OPT} -fwrapv)
    foreach(source ${mpeg2_sources})
        get_filename_component(name "${source}" NAME)
        if(name MATCHES "^(decode|slice|idct|motion_comp)\\.c$")
            set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS -O3)
        endif()
    endforeach()
    add_executable(mpeg2_bench ${root}/tools/bench_main.c
        ${root}/examples/app_mpeg2_benchmark/src/mpeg2_benchmark.c ${root}/examples/app_mpeg2_benchmark/src/arena.c)
    target_include_directories(mpeg2_bench PRIVATE ${root}/examples/app_mpeg2_benchmark/src)
    target_compile_definitions(mpeg2_bench PRIVATE _POSIX_C_SOURCE=200809L)
    option(MPEG_BENCH_ALLOW_MPEG1 "Allow identical MPEG-1 input in comparisons" OFF)
    set(reference_args "")
    if(MPEG_BENCH_ALLOW_MPEG1)
        target_compile_definitions(mpeg2_bench PRIVATE MPEG_BENCH_ALLOW_MPEG1=1)
        list(APPEND reference_args --allow-mpeg1)
    endif()
    target_compile_options(mpeg2_bench PRIVATE ${MPEG_COLD_OPT} -Wall -Wextra -Werror)
    target_link_libraries(mpeg2_bench PRIVATE mpeg2_scalar)
    add_executable(mpeg2_arena_test arena_test.c ${root}/examples/app_mpeg2_benchmark/src/arena.c)
    target_include_directories(mpeg2_arena_test PRIVATE ${root}/examples/app_mpeg2_benchmark/src)
    target_link_libraries(mpeg2_arena_test PRIVATE mpeg2_scalar)
    target_compile_options(mpeg2_arena_test PRIVATE ${MPEG_COLD_OPT} -Wall -Wextra -Werror)
    set(decoder_target mpeg2_scalar)
    set(test_targets mpeg2_bench mpeg2_arena_test)
    add_test(NAME mpeg2_arena COMMAND mpeg2_arena_test)
    add_test(NAME mpeg2_reference COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_decode.py --bench $<TARGET_FILE:mpeg2_bench>
        --work ${CMAKE_CURRENT_BINARY_DIR}/fixtures ${reference_args})
    set_tests_properties(mpeg2_arena mpeg2_reference PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1" TIMEOUT 120)
else()
    message(FATAL_ERROR "Unknown MPEG_BACKEND: ${MPEG_BACKEND}")
endif()
foreach(target ${test_targets})
    target_compile_options(${target} PRIVATE -UNDEBUG)
endforeach()
if(MPEG_SANITIZE)
    # Legacy integer transforms shift negative fixed-point values.
    target_compile_options(${decoder_target} PRIVATE -fsanitize=address,undefined -fno-sanitize=shift)
    foreach(target ${test_targets})
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
    if(TARGET plmpeg_upstream_ref)
        target_compile_options(plmpeg_upstream_ref PRIVATE -fno-sanitize=shift)
        foreach(target mp2_bench mp2_bench_fast mp2_bench_window mp2_upstream)
            target_compile_options(${target} PRIVATE -fno-sanitize=shift)
        endforeach()
    endif()
endif()
