project(Tools C CXX)
cmake_minimum_required(VERSION 3.15.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(Common)

set(INSTALL_DIR "Tools")

add_subdirectory("MiniBrowser")

set(TOOLS_TARGETS MiniBrowser)

if (${ALLINONE_BUILD})
    foreach(target ${TOOLS_TARGETS})
		add_dependencies(${target} GenerateSDK)
    endforeach()

    add_custom_target(install_tools ALL "${CMAKE_COMMAND}" 
        -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_install.cmake" 
        DEPENDS ${TOOLS_TARGETS}) 
endif ()