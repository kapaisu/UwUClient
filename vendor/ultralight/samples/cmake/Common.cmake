include(Platform)

if (${UL_ENABLE_STATIC_BUILD})
    include(StaticLibs)
endif ()

set(SDK_ROOT "${UL_SDK_PATH}")

set(ULTRALIGHT_INCLUDE_DIR "${SDK_ROOT}/include")
set(ULTRALIGHT_BINARY_DIR "${SDK_ROOT}/bin")
set(ULTRALIGHT_INSPECTOR_DIR "${SDK_ROOT}/inspector")
set(ULTRALIGHT_RESOURCES_DIR "${SDK_ROOT}/resources")
set(ULTRALIGHT_LIBRARY_DIR "${SDK_ROOT}/bin"
                           "${SDK_ROOT}/lib")

get_filename_component(INFO_PLIST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in" REALPATH)
get_filename_component(ENTITLEMENTS_PLIST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Entitlements.plist" REALPATH)

macro(add_console_app APP_NAME)
    include_directories("${ULTRALIGHT_INCLUDE_DIR}")
    link_directories("${ULTRALIGHT_LIBRARY_DIR}")
    link_libraries(UltralightCore Ultralight WebCore AppCore)

    if (${UL_ENABLE_STATIC_BUILD})
        add_definitions(-DULTRALIGHT_STATIC_BUILD)
        link_libraries(${UL_STATIC_LIBS})
    endif ()

    if (UL_PLATFORM MATCHES "macos")
        SET(CMAKE_INSTALL_RPATH ".")
    endif ()

    add_executable(${APP_NAME} ${ARGN})

    # Always link to the C++ standard library
    set_target_properties(${APP_NAME} PROPERTIES LINKER_LANGUAGE CXX)

    set(INSTALL_PATH "${INSTALL_DIR}/${APP_NAME}")

    INSTALL(TARGETS ${APP_NAME}
        RUNTIME DESTINATION "${INSTALL_PATH}"
        BUNDLE  DESTINATION "${INSTALL_PATH}")

    INSTALL(DIRECTORY "${ULTRALIGHT_BINARY_DIR}/" DESTINATION "${INSTALL_PATH}")
    INSTALL(DIRECTORY "${ULTRALIGHT_RESOURCES_DIR}" DESTINATION "${INSTALL_PATH}/assets")
    INSTALL(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION "${INSTALL_PATH}/assets" OPTIONAL)
endmacro ()

macro(add_app APP_NAME)
    include_directories("${ULTRALIGHT_INCLUDE_DIR}")
    link_directories("${ULTRALIGHT_LIBRARY_DIR}")
    link_libraries(UltralightCore AppCore Ultralight WebCore)

    if (${UL_ENABLE_STATIC_BUILD})
        add_definitions(-DULTRALIGHT_STATIC_BUILD)
        link_libraries(${UL_STATIC_LIBS} ${APPCORE_STATIC_LIBS})
    endif ()

    add_executable(${APP_NAME} WIN32 MACOSX_BUNDLE ${ARGN})

    # Always link to the C++ standard library
    set_target_properties(${APP_NAME} PROPERTIES LINKER_LANGUAGE CXX)

    set(INSTALL_PATH "${INSTALL_DIR}/${APP_NAME}")

    if (UL_PLATFORM MATCHES "macos")
        # Include Entitlements.plist
        set_source_files_properties(${ENTITLEMENTS_PLIST_PATH} PROPERTIES MACOSX_PACKAGE_LOCATION "Contents")

        # Enable High-DPI on macOS through our custom Info.plist template
        set_target_properties(${APP_NAME} PROPERTIES
            BUNDLE True
            MACOSX_BUNDLE_GUI_IDENTIFIER ultralight.${APP_NAME}
            MACOSX_BUNDLE_BUNDLE_NAME ${APP_NAME}
            MACOSX_BUNDLE_EXECUTABLE_NAME ${APP_NAME}
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            MACOSX_BUNDLE_INFO_PLIST ${INFO_PLIST_PATH}
        )
        
        # Set the install destination for the app bundle
        set(BUNDLE_INSTALL_PATH "${INSTALL_PATH}/${APP_NAME}.app")
        set(BUNDLE_EXEC_PATH "${BUNDLE_INSTALL_PATH}/Contents/MacOS")
        set(BUNDLE_RESOURCE_PATH "${BUNDLE_INSTALL_PATH}/Contents/Resources")
        set(BUNDLE_ASSETS_PATH "${BUNDLE_RESOURCE_PATH}/assets")

        install(TARGETS ${APP_NAME} BUNDLE DESTINATION "${INSTALL_PATH}")
            
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION "${BUNDLE_ASSETS_PATH}" OPTIONAL)
        install(DIRECTORY "${ULTRALIGHT_RESOURCES_DIR}" DESTINATION "${BUNDLE_ASSETS_PATH}")
        if (NEEDS_INSPECTOR)
            install(DIRECTORY "${ULTRALIGHT_INSPECTOR_DIR}" DESTINATION "${BUNDLE_ASSETS_PATH}")
        endif ()
        install(DIRECTORY "${ULTRALIGHT_BINARY_DIR}/" DESTINATION "${BUNDLE_EXEC_PATH}")
    else ()
        if (UL_PLATFORM MATCHES "windows")
            # Use main instead of WinMain for Windows subsystem executables
            set_target_properties(${APP_NAME} PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
        endif()

        set(ASSETS_PATH "${INSTALL_PATH}/assets")
        set(BIN_PATH "${INSTALL_PATH}")

        install(TARGETS ${APP_NAME} RUNTIME DESTINATION "${INSTALL_PATH}")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION "${ASSETS_PATH}" OPTIONAL)
        install(DIRECTORY "${ULTRALIGHT_RESOURCES_DIR}" DESTINATION "${ASSETS_PATH}")
        if (NEEDS_INSPECTOR)
            install(DIRECTORY "${ULTRALIGHT_INSPECTOR_DIR}" DESTINATION "${ASSETS_PATH}")
        endif ()
        install(DIRECTORY "${ULTRALIGHT_BINARY_DIR}/" DESTINATION "${BIN_PATH}")
    endif ()
endmacro ()
