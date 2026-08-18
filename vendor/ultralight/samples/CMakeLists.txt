project(Samples C CXX)
cmake_minimum_required(VERSION 3.15.0)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(Common)

set(INSTALL_DIR "Samples")

add_subdirectory("Sample 1 - Render to PNG")
add_subdirectory("Sample 2 - Basic App")
add_subdirectory("Sample 3 - Resizable App")
add_subdirectory("Sample 4 - JavaScript")
add_subdirectory("Sample 5 - File Loading")
add_subdirectory("Sample 6 - Intro to C API")
add_subdirectory("Sample 7 - OpenGL Integration")
add_subdirectory("Sample 8 - Web Browser")
add_subdirectory("Sample 9 - Multi Window")

set(SAMPLES_TARGETS Sample1 Sample2 Sample3 Sample4 Sample5 Sample6 Sample7 Sample8 Sample9)

if (${ALLINONE_BUILD})
    foreach(target ${SAMPLES_TARGETS})
        add_dependencies(${target} GenerateSDK)
    endforeach()

    add_custom_target(install_samples ALL "${CMAKE_COMMAND}" 
        -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_install.cmake" 
        DEPENDS ${SAMPLES_TARGETS}) 
endif ()