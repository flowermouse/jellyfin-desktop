add_sources(PlayerComponent.cpp PlayerComponent.h)
add_sources(AlbumArtProvider.cpp AlbumArtProvider.h)
add_sources(MpvVideoItem.cpp MpvVideoItem.h)
add_sources(OpenGLDetect.cpp OpenGLDetect.h)
add_sources(QtHelper.h)

# External IINA playback. The argument/protocol helpers are plain Qt and build everywhere so they
# stay unit testable; the component itself needs NSWorkspace and is macOS only.
add_sources(IinaLaunchArgs.cpp IinaLaunchArgs.h)
add_sources(IinaSessionState.cpp IinaSessionState.h)
add_sources(MpvIpcClient.cpp MpvIpcClient.h)

if(APPLE)
  add_sources(IinaPlayerComponent.cpp IinaPlayerComponent.h)
  add_subdirectory(osx)
endif()
