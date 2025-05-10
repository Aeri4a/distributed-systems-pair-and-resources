cmake_minimum_required(VERSION 3.30)
project(distributed)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_COMPILER /usr/lib64/openmpi/bin/mpic++)

#include_directories("/usr/include/openmpi-x86_64")
set(MPI_HOME /usr/lib64/openmpi)
find_package(MPI REQUIRED)
include_directories(SYSTEM /usr/include/openmpi-x86_64)

add_executable(distributed main.cpp
        PairQueue.cpp
        PairQueue.hpp)
target_link_libraries(distributed PRIVATE MPI::MPI_CXX)
