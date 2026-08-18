# EXPERIMENTAL:
# Set this to TRUE to embed assets directly into the executable.
set(SHOULD_EMBED_FILES FALSE)

set(SOURCE_FILES "src/Browser.h"
                 "src/Browser.cpp"
                 "src/Page.h"
                 "src/Page.cpp"
                 "src/UI.h"
                 "src/UI.cpp"
                 "src/main.cpp")

if (SHOULD_EMBED_FILES)
	set(SOURCE_FILES ${SOURCE_FILES}
					 "src/platform/EmbeddedFileSystem.h"
					 "src/platform/EmbeddedFileSystem.cpp")
    add_app(MiniBrowser EMBED_FILES NEEDS_INSPECTOR SOURCES ${SOURCE_FILES})
else ()
    add_app(MiniBrowser NEEDS_INSPECTOR SOURCES ${SOURCE_FILES})
endif ()
