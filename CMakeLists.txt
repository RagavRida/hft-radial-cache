cmake_minimum_required(VERSION 3.10)
project(HFTRadialCache VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# Platform-specific flags
if(UNIX AND NOT APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mtune=native")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NUMA REQUIRED numa)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/radial_circular_list.cpp
    src/memory_manager.cpp
    src/metrics.cpp
    src/error_handler.cpp
    src/persistent_cache.cpp
    src/security_manager.cpp
    src/advanced_operations.cpp
    src/multi_level_cache.cpp
    src/bloom_filter.cpp
    src/skip_list.cpp
    src/b_tree.cpp
)

# Header files
set(HEADERS
    include/radial_circular_list.hpp
    include/lockfree_map.hpp
    include/lockfree_heap.hpp
    include/midpoint.hpp
    include/node.hpp
    include/config.hpp
    include/memory_manager.hpp
    include/lockfree_queue.hpp
    include/metrics.hpp
    include/error_handler.hpp
    include/persistent_cache.hpp
    include/security_manager.hpp
    include/advanced_operations.hpp
    include/multi_level_cache.hpp
    include/bloom_filter.hpp
    include/skip_list.hpp
    include/b_tree.hpp
    include/simd_operations.hpp
    include/advanced_memory_pool.hpp
)

# Main executable
add_executable(hft_cache ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(hft_cache 
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Platform-specific linking
if(UNIX AND NOT APPLE)
    target_link_libraries(hft_cache ${NUMA_LIBRARIES})
    target_include_directories(hft_cache PRIVATE ${NUMA_INCLUDE_DIRS})
endif()

# Test executable
enable_testing()
add_executable(hft_cache_tests tests/test_main.cpp ${SOURCES} ${HEADERS})

# Link test libraries
target_link_libraries(hft_cache_tests 
    gtest
    gtest_main
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Platform-specific linking for tests
if(UNIX AND NOT APPLE)
    target_link_libraries(hft_cache_tests ${NUMA_LIBRARIES})
    target_include_directories(hft_cache_tests PRIVATE ${NUMA_INCLUDE_DIRS})
endif()

# Add tests
add_test(NAME HFT_Cache_Tests COMMAND hft_cache_tests)

# Install targets
install(TARGETS hft_cache
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES ${HEADERS} DESTINATION include/hft_cache)

# Package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/HFTRadialCacheConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Documentation
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    
    add_custom_target(docs ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()

# Performance profiling
option(ENABLE_PROFILING "Enable performance profiling" OFF)
if(ENABLE_PROFILING)
    target_compile_options(hft_cache PRIVATE -pg)
    target_compile_options(hft_cache_tests PRIVATE -pg)
    target_link_options(hft_cache PRIVATE -pg)
    target_link_options(hft_cache_tests PRIVATE -pg)
endif()

# Address sanitizer
option(ENABLE_SANITIZER "Enable address sanitizer" OFF)
if(ENABLE_SANITIZER)
    target_compile_options(hft_cache PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_compile_options(hft_cache_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(hft_cache PRIVATE -fsanitize=address)
    target_link_options(hft_cache_tests PRIVATE -fsanitize=address)
endif()

# Thread sanitizer
option(ENABLE_THREAD_SANITIZER "Enable thread sanitizer" OFF)
if(ENABLE_THREAD_SANITIZER)
    target_compile_options(hft_cache PRIVATE -fsanitize=thread)
    target_compile_options(hft_cache_tests PRIVATE -fsanitize=thread)
    target_link_options(hft_cache PRIVATE -fsanitize=thread)
    target_link_options(hft_cache_tests PRIVATE -fsanitize=thread)
endif()

# Print configuration summary
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Profiling enabled: ${ENABLE_PROFILING}")
message(STATUS "Address sanitizer enabled: ${ENABLE_SANITIZER}")
message(STATUS "Thread sanitizer enabled: ${ENABLE_THREAD_SANITIZER}")