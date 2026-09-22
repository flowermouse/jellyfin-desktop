# Apple Silicon only

This fork builds one thing: a Jellyfin Desktop app for arm64 macOS, playing video
in the built-in mpv player. Everything that existed to serve another platform or
another architecture has been removed.

## Where the size went

The bundle went from 727 MB to 369 MB. Almost none of that came from deleting
source.

| Step | Saved |
| --- | --- |
| Thinning universal binaries to arm64 | ~308 MB |
| Dropping 49 of 53 Chromium locale packs | ~37 MB |
| Dropping the devtools front-end pack | ~11 MB |
| Dropping the x86_64 V8 snapshot | ~0.7 MB |
| Deleting non-macOS source | 0 |

Qt is distributed as `x86_64 + arm64` frameworks, so on a single-architecture
build half of every Mach-O file in the bundle is unreachable. `QtWebEngineCore`
alone went from 448 MB to 215 MB. Both steps live in
`CMakeModules/CompleteBundleMac.cmake.in`, after every `install_name_tool` pass
and before signing: rewriting or deleting anything inside a bundle invalidates
its signature.

The floor is now `QtWebEngineCore` at 280 MB of the remaining 369 MB. Getting
under that means building Qt from source with WebEngine features disabled.

Deleting platform source saved nothing, because none of it was being compiled -
it was all behind `if(WIN32)`, `if(LINUX_DBUS)` and the like. It was removed to
stop it being carried and maintained, not to shrink anything.

## What was removed

**Whole trees:** `src/mpris` (Linux D-Bus media control), `src/display/{win,x11,rpi}`,
`src/system/openelec`, `src/utils/win`, `bundle/win`, `dev/{windows,linux,appimage}`,
`debian`, `deployment`.

**Platform variants:** the Windows taskbar component and its Qt5 compatibility
stubs, the Windows/X11/D-Bus power components, and the CEC and LIRC inputs.
`PowerComponent::Get()` and `TaskbarComponent::Get()` were `#ifdef` ladders and
are now single returns; on macOS the taskbar component was always the do-nothing
base class, and it is kept only because `WindowManager` still calls `setWindow()`
on it.

**Build configuration:** `Win32Configuration`, `LinuxConfiguration`,
`CompleteBundleWin`, `WindowsInstaller`, `InstallLinuxDesktopFile`,
`PreparePortableZip`, `FindCEC`, `FindGLES2`, `FindDL`, and the Windows, Linux
and AppImage CI workflows. `CPACK_SYSTEM_NAME` said `macosx-x86_64` and now says
`macosx-arm64`.

**Settings:** the `cec` section of `settings_description.json`, which no longer
had code behind it.

## What was deliberately kept

- **Apple Remote and Apple media keys** (`src/input/apple`). Unlike the external
  player fork, playback happens in this process, so the media keys reach
  something.
- **`src/display/dummy`.** `DisplayManager` falls back to it when the macOS
  backend cannot initialise.
- **SDL2 gamepad input.** Still works on macOS.
- **Four Chromium locale packs** (`en-US`, `en-GB`, `zh-CN`, `zh-TW`). A missing
  locale falls back to `en-US`, so that one has to stay.

## Consequences worth knowing

Removing `qtwebengine_devtools_resources.pak` takes away the devtools *front-end*
only. `--remote-debugging-port` still opens the endpoint, so anything speaking
CDP directly still works; opening the inspector window in a browser does not.

Chromium's own strings - context menus, form validation, network error pages -
fall back to English outside the four locales kept. Every string Jellyfin itself
renders comes from the web client and is unaffected.
