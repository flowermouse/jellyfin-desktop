# AGENTS.md

## Project context

This repository is a fork of the official Jellyfin Desktop client. The primary development target
for this fork is macOS. The current product goal is to improve the libmpv playback experience,
especially the player UI and interaction design, without regressing playback behavior or making
upstream synchronization unnecessarily difficult.

Treat the existing application as a working baseline. Prefer focused, reviewable changes over broad
rewrites, and preserve cross-platform behavior unless a task is explicitly macOS-only.

## Architecture map

Jellyfin Desktop is a Qt 6 application that embeds Jellyfin Web in Qt WebEngine and renders video
through libmpv/MpvQt.

- `src/ui/webview.qml`: top-level Qt Quick window. The `MpvVideoItem` is behind the transparent
  `WebEngineView`; the web UI/OSD is layered above video.
- `src/player/PlayerComponent.{h,cpp}`: native playback API exposed to JavaScript, mpv state and
  events, playlist handling, stream selection, volume, seeking, and video rectangle control.
- `src/player/MpvVideoItem.{h,cpp}`: MpvQt-backed Qt Quick video item and controller lifecycle.
- `native/mpvVideoPlayer.js`: Jellyfin Web media-player adapter. It connects the web-client player
  lifecycle and OSD to the native `player` API.
- `native/nativeshell.js` and `native/inputPlugin.js`: native bridge and input integration.
- `native/find-webclient.*`: discovery/bootstrap resources for loading Jellyfin Web.
- `resources/`: images, sounds, settings descriptions, input maps, metadata, and test media.
- `src/display/osx`, `src/input/apple`, `src/power/*Mac*`, and `src/utils/osx`: macOS-specific native
  integration.
- `dev/macos/`: supported setup, build, run, test, and bundle scripts.
- `tests/`: Qt Test unit tests, registered with CTest.

Resources under `native/` and `resources/` are collected by CMake. When adding a normal resource,
do not hand-edit a `.qrc`; verify that the generated-resource step includes it.

## Player UI boundaries

Before changing player visuals, identify which layer owns the behavior:

1. Use web-layer JavaScript/CSS for Jellyfin OSD styling, controls, DOM interaction, and web-client
   playback integration.
2. Use QML for native window/video composition, stacking, transparency, focus, and geometry.
3. Use C++ only when a native capability, mpv property/event, Qt bridge API, or platform integration
   is required.

Do not duplicate playback state independently across these layers. `PlayerComponent`/mpv is the
native source for playback state; `mpvVideoPlayer.js` translates it into Jellyfin Web events.
Keep signal connections and disconnections symmetric, and avoid accumulating anonymous signal
handlers each time playback starts.

The web client is loaded remotely and can vary by Jellyfin server version. DOM selectors and private
web-client APIs are compatibility boundaries: scope selectors narrowly, tolerate missing elements,
and fail gracefully. Avoid replacing large upstream OSD structures when styling or a small adapter
is sufficient.

Preserve these playback invariants unless the task explicitly changes them:

- The `WebEngineView` remains transparent over the mpv video item.
- Fullscreen/window state and the web client's OSD stay synchronized.
- Starting, stopping, errors, navigation, and repeated playback do not leave stale overlays,
  listeners, timers, or body classes behind.
- Mouse, keyboard, Apple media keys, focus, and auto-hide behavior remain usable.
- Seeking, pause/resume, volume/mute, subtitle/audio selection, playback rate, and buffered ranges
  continue to report consistent state.
- UI changes do not alter transcoding/direct-play decisions unless requested.

For macOS-only behavior, prefer a clear `Q_OS_MAC`/CMake platform guard or the existing `osx`/`apple`
directories. Do not fork shared logic merely because macOS is the primary test platform.

## Development workflow on macOS

From the repository root:

```sh
dev/macos/setup.sh   # first-time dependency installation; do not run without user approval
dev/macos/build.sh
dev/macos/test.sh
dev/macos/run.sh
```

The standard development app is `build/src/Jellyfin Desktop.app`. A bundled release app is written
to `build/output/Jellyfin Desktop.app` by `dev/macos/bundle.sh`.

Use the provided scripts instead of inventing a parallel build configuration. `build/` and
`dev/macos/deps/` are generated and must not be committed. Do not delete or recreate `build/` unless
a clean build is actually needed; if deletion would discard useful local state, ask first.

Useful focused commands after configuration include:

```sh
cmake --build build
dev/macos/test.sh --output-on-failure
ctest --test-dir build -R <test-name> --output-on-failure
```

If the application needs to be launched for verification, use `dev/macos/run.sh`. Logs are stored in
`~/Library/Logs/Jellyfin Desktop/<profile-id>/`. Do not modify or remove a user's profiles, server
credentials, cache, or logs.

## Change guidelines

- Read the full call path before editing cross-boundary behavior: web event -> WebChannel bridge ->
  `PlayerComponent` -> MpvQt/libmpv, and back through Qt signals.
- Keep changes compatible with the repository's Qt and CMake versions. The macOS setup currently
  pins Qt in `dev/macos/common.sh`; do not silently change the toolchain or dependency versions.
- Preserve public `Q_INVOKABLE` methods, signals, web-player method names, and payload units unless
  all callers are updated. Pay special attention to milliseconds versus seconds/ticks and volume
  ranges.
- Keep main-thread/UI-thread constraints in mind for Qt Quick, WebEngine, and mpv callbacks.
- Reuse existing components and visual tokens where possible. New player UI should support small
  windows, fullscreen, Retina scaling, long/localized labels, hover/focus/pressed/disabled states,
  and reduced-motion/accessibility expectations.
- Avoid permanent diagnostic logging in hot paths such as position updates, mouse movement, render
  callbacks, and buffering events. Never log media URLs containing access tokens or credentials.
- Do not edit vendored code in `external/` unless the task specifically requires it. Prefer changes
  at the integration boundary.
- Avoid unrelated cleanup in feature commits so this fork can continue to absorb upstream changes.

## Style

- C++ and Objective-C++ follow `.clang-format`: 2-space indentation, no tabs, 100-column target,
  Allman braces, and project naming conventions. Format only touched code where practical.
- Follow the surrounding style in QML, JavaScript, shell, and CMake files; do not mechanically
  reformat whole files.
- Use Qt parent ownership and existing RAII patterns. Avoid unmanaged ownership and unsafe captures.
- Add comments for non-obvious lifecycle, compatibility, threading, or rendering constraints, not
  for code that is already self-explanatory.

## Validation expectations

Run the narrowest useful check during iteration, then validate in proportion to the change:

- C++/QML/CMake/resource changes: build with `dev/macos/build.sh` (or `cmake --build build` when the
  existing configuration is valid).
- Native logic changes: run `dev/macos/test.sh --output-on-failure`; add or update a Qt Test when the
  behavior is reasonably unit-testable.
- JavaScript changes: at minimum perform a syntax check when an available local tool supports it,
  then exercise the affected Jellyfin Web flow in the app.
- Player UI changes require manual visual/interaction verification with actual video. Check windowed
  and fullscreen modes, playback start/stop/restart, pause/seek, control auto-hide and reappearance,
  keyboard/mouse interaction, and at least one failure/exit path.
- Changes to rendering, transparency, or geometry should also be checked during resize and after
  entering/leaving fullscreen. When relevant, verify Retina scaling and multiple-display behavior.

Tests do not replace visual playback verification. If a required check cannot be run because Qt,
mpv, a Jellyfin server, credentials, or media are unavailable, state exactly what was not verified.

## Repository hygiene

- Preserve user changes in a dirty worktree and do not overwrite unrelated edits.
- Do not commit generated build products, dependency downloads, app bundles, logs, credentials,
  server URLs, or profile data.
- Do not update submodules, dependencies, lock/pin versions, or CI configuration unless requested.
- Summarize modified files and verification performed when handing work back.
