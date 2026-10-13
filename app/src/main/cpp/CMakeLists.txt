cmake_minimum_required(VERSION 3.22.1)
project("native-app" C)

set(CMAKE_C_STANDARD 11)

add_library(native-app SHARED
    main.c
    ui_renderer.c
    wifi_stats.c
    font8x16.c
    tt_font.c
)

target_link_libraries(native-app
    android
    log
    m
)
