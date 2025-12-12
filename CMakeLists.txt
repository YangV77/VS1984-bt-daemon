cmake_minimum_required(VERSION 3.27)
project(vs1984-btd)

add_executable(vs1984-btd
        src/bt_core.cpp
        src/bt_core.hpp
        src/bt_api.cpp
        src/bt_api.h
        src/bt_daemon.c
        src/bt_utils.c
        src/bt_utils.h
)


target_link_libraries(vs1984-btd PRIVATE uv
        torrent-rasterbar
        cjson
        pthread dl
        stdc++)
