# External IINA playback is the only backend in this build. The argument, protocol and state
# helpers are plain Qt so they stay unit testable; discovery needs NSWorkspace.
add_sources(IinaLaunchArgs.cpp IinaLaunchArgs.h)
add_sources(IinaSessionState.cpp IinaSessionState.h)
add_sources(MpvIpcClient.cpp MpvIpcClient.h)
add_sources(IinaPlayerComponent.cpp IinaPlayerComponent.h)

add_subdirectory(osx)
