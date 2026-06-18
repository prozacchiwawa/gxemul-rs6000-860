add_library(console OBJECT
    x11.cc
    console.cc
)

if (WIN32)
    target_sources(console PRIVATE console_win32.cc)
else()
    target_sources(console PRIVATE console_unix.cc)
endif()
