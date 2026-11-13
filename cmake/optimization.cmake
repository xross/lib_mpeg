# One profile switch permits an all-O3 control build with identical sources.
option(MPEG_SPACE_OPTIMIZE "Use -Os on cold files; retain -O3 on decoder kernels" ON)
if(MPEG_SPACE_OPTIMIZE)
    set(MPEG_COLD_OPT -Os)
else()
    set(MPEG_COLD_OPT -O3)
endif()
