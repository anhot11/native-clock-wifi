cmake_minimum_required(VERSION 3.22.1)
project("native-app" C)

set(CMAKE_C_STANDARD 11)

add_library(native-app SHARED
    main.c
    ui.c
    render.c
    text.c
    theme.c
    sys_cpu.c
    sys_mem.c
    sys_net.c
    sys_batt.c
    sys_bridge.c
    bench.c
)

target_link_options(native-app PRIVATE
    -Wl,--gc-sections
)

target_link_libraries(native-app
    android
    log
    m
)
