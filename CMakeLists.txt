cmake_minimum_required(VERSION 4.0)
project(ProstVM C)

set(CMAKE_C_STANDARD 23)

add_executable(ProstVM main.c
        prost/prost.h)

add_executable(depbc depbc.c
        prost/prost.h)


if(UNIX)
    target_compile_options(ProstVM PRIVATE -g -ggdb)
    target_link_libraries(ProstVM m)
endif()