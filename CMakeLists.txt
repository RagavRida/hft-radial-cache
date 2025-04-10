cmake_minimum_required(VERSION 3.10)
project(HFTRadialCache)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -pthread -march=native")
find_library(NUMA_LIBRARY numa)
if (NUMA_LIBRARY)
    add_definitions(-DHAVE_NUMA)
endif()

include_directories(include)

add_executable(hft_cache src/main.cpp src/radial_circular_list.cpp)
if (NUMA_LIBRARY)
    target_link_libraries(hft_cache PRIVATE pthread ${NUMA_LIBRARY})
else()
    target_link_libraries(hft_cache PRIVATE pthread)
endif()