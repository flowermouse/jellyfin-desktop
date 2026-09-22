add_subdirectory(osx)

# Still built: DisplayManager falls back to it when the macOS backend cannot be initialised.
add_subdirectory(dummy)

find_all_sources(. SRC)
add_sources(${SRC})