# Disable various GLFW options
option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
option(GLFW_INSTALL "Generate installation target" OFF)

set(GLFW_DIR "glfw")
add_subdirectory(${GLFW_DIR})
include_directories("${GLFW_DIR}/include")
include_directories("${GLFW_DIR}/deps")

link_libraries(glfw)

set(GLAD_SOURCES "${GLFW_DIR}/deps/glad/glad.h"
                 "${GLFW_DIR}/deps/glad.c")

set(SOURCES "src/Sample.h"
            "src/Sample.cpp"
            "src/WebTile.h"
            "src/WebTile.cpp"
            "src/Window.h"
            "src/Window.cpp"
            "src/GLTextureSurface.h"
            "src/GLTextureSurface.cpp"
            "src/main.cpp")

add_app(Sample7 ${SOURCES} ${GLAD_SOURCES})