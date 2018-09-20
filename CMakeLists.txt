cmake_minimum_required(VERSION 2.8)
project(demo)

add_executable(demo main.c )
target_link_libraries(demo -lm)