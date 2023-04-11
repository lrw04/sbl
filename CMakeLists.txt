cmake_minimum_required(VERSION 3.14)
project(sbl)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

add_executable(sbl-prep prep.cpp)
add_executable(sbl-run run.cpp)
add_executable(sbl-clean clean.cpp)
