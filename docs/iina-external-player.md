# External IINA video playback (macOS)

This build does not render video itself. It hands the stream Jellyfin Web selected to the user's
own installation of [IINA](https://iina.io), which plays it in its own application window.

Scope of this branch:

- **macOS on Apple Silicon only.** The build is pinned to arm64 and the bundle is thinned to a
  single architecture at deploy time; Intel Macs, Windows and Linux are not supported.
- **No audio playback.** libmpv and the whole built-in player are gone, so the music library is
  browsable but not playable. Audio-related settings, Now Playing / media-key integration, MPRIS
  and album art went with it.
- **No in-window renderer.** There is no video surface, no transparency layer and no local OSD.

## Bundle size

Removing the mpv stack is the small half of this. Measured on a deployed bundle:

| | |
| --- | --- |
| Before | 725 MB |
| After removing libmpv, ffmpeg and the codec libraries | 669 MB |
| After thinning to arm64 | 363 MB |

`QtWebEngineCore` is 280 MB of the remaining 363 MB. Trimming further would mean building Qt from
source with WebEngine features disabled; the Qt QML modules macdeployqt over-collects are only
worth another ~15 MB and it deploys them whether or not the QML imports them.

## Scope

Direct Play only. The device profile in `native/nativeshell.js` already direct-plays any video
unless the user turns on one of the `force_transcode_*` settings, so IINA receives the original
file and sees every track it contains. If the server does transcode anyway, playback still works
(mpv handles the HLS stream) but track selection degrades to whatever the transcode produced.

Subtitles and audio are assumed to be **embedded in the media**:

- Embedded tracks are selected by mpv track index (`aid`/`sid`), even when the server reports
  `DeliveryMethod: 'External'` for a text subtitle. Under Direct Play that track is physically in
  the container, so fetching it again over HTTP would only duplicate data and put the access token
  into a second URL.
- Sidecar subtitle files stored next to the video **on the server** are not supported. They are not
  in the container, so they are simply unavailable; selecting one in Jellyfin turns subtitles off.
- A subtitle file on the **user's own machine** is dragged straight onto the IINA window, which
  IINA handles itself (`PlayerCore.openFromPasteboard` → `loadExternalSubFile`). This integration
  neither helps nor interferes: `sid` is never observed, so a dragged subtitle is not overridden
  unless the user afterwards picks a track from Jellyfin's own menu. IINA's filename-based subtitle
  auto-loading does not apply, because the media is an HTTP URL rather than a local file.

`IinaLaunchArgs::externalTrackUrl()` is kept purely as a guard: a `"#,<url>"` selection reaching
the launch path is dropped rather than placed on the command line, so an access token cannot end up
in argv that way.

Track changes made inside IINA's own menus are not reported back to Jellyfin. `aid`/`sid`/
`track-list` are deliberately not observed, so Jellyfin's menu and the server session keep showing
the track that was selected through Jellyfin.

## Window title

IINA titles a network stream from mpv's `media-title`, which for a Jellyfin stream URL would be
the item id and access token. `native/mpvVideoPlayer.js` therefore composes a title from the item
the server described — `Series - S01E02 - Episode` for an episode, `Movie (Year)` for a movie,
the plain name otherwise — and passes it over the bridge as `metadata.title`;
`IinaLaunchArgs::buildCliArguments()` turns it into `--mpv-force-media-title=<title>`. An item with
no usable name sends nothing and leaves IINA's own naming in place.

The title is composed on the web side because that is the only side that sees the Jellyfin item.

## Ending playback

Nothing auto-advances. Whether an item finishes, is stopped, or its IINA window is closed, the
session ends and the user is left on the Jellyfin page they started from; the next episode is
started by hand.

This is a deliberate departure from Jellyfin's usual behaviour and from the original plan, which
left the queue to Jellyfin Web. With video in a separate application, auto-advance means a fresh
IINA window appearing unprompted — and because closing that window is itself a stop, it loops for
as long as the queue has items.

The mechanism matters: `PlaybackManager` only clears its `_playNextAfterEnded` flag inside its own
`stop()`, and `onPlaybackStopped` consults that flag to decide whether to queue the next item. A
player that reports `'stopped'` directly is indistinguishable from media ending naturally, so the
adapter routes every end of playback through `playbackManager.stop()` instead. Suppressing the
next item does not affect the stop report itself, so the server still receives the final position
and still marks a fully watched item as played.

There is no other backend left to behave differently.

## Runtime dependency

- The official stable IINA release, bundle identifier `com.colliderli.iina`, must be installed.
- IINA is **not** bundled, built, modified or redistributed by this project. `external/iina` is a
  read-only reference checkout used to verify IINA's public behavior; nothing in it is compiled.
- IINA is located through `NSWorkspace URLForApplicationWithBundleIdentifier:`, so any install
  location works. `Contents/MacOS/iina-cli` inside the discovered bundle is the launcher.
- If IINA is missing, playback fails with a prompt offering the download page. There is nothing to
  fall back to: this build has no renderer of its own.

## How it works

```text
Jellyfin Web
  -> native/mpvVideoPlayer.js        (getNativeVideoBackend())
  -> WebChannel: window.api.iinaPlayer
  -> IinaPlayerComponent -> QProcess -> iina-cli -> IINA.app / libmpv

IINA / mpv JSON IPC (unix socket)
  -> MpvIpcClient -> IinaSessionState -> Qt signals
  -> mpvVideoPlayer.js events -> Jellyfin Web session reporting
```

Source layout:

| File | Responsibility |
| --- | --- |
| `src/player/osx/IinaApplication.{h,mm}` | Locate IINA.app and `iina-cli` |
| `src/player/IinaLaunchArgs.{h,cpp}` | Build `iina-cli` argv, socket paths, unit conversion |
| `src/player/MpvIpcClient.{h,cpp}` | mpv JSON IPC framing and transport |
| `src/player/IinaSessionState.{h,cpp}` | Playback session state machine |
| `src/player/IinaPlayerComponent.{h,cpp}` | WebChannel component, process and session lifecycle |

`IinaPlayerComponent` is the only player component. It replaced `PlayerComponent`, whose window,
MpvQt render loop and local playback state had no meaning for a separate application, rather than
deriving from it.

Units: everything crossing the WebChannel is in milliseconds; mpv's `time-pos`/`duration` are in
seconds. All conversion happens in `IinaLaunchArgs` and `IinaPlayerComponent`. Buffered ranges are
reported in Jellyfin's 100ns ticks, the unit Jellyfin Web expects.

## Verification of IINA's public behavior

The design below was derived by reading the IINA sources in `external/iina` at the checked-out
revision, before IINA was available to test against. It has since been exercised against IINA
1.4.4 and a real Jellyfin server: playback, seeking, and audio/subtitle track switching work. The
one defect that testing found — closing the IINA window starting the next episode, in a loop — is
fixed and described under "Ending playback" above. Everything under "Manual verification still
required" remains unverified.

1. **`iina-cli` flags** (`iina-cli/main.swift`): `--mpv-<name>=<value>` passes arbitrary mpv
   options through, `--separate-windows`/`-w`, `--stdin`/`--no-stdin`, `--keep-running`,
   `--music-mode`, `--pip`. It resolves `Contents/MacOS/IINA` next to itself and launches it with
   the arguments; it is not a general IPC channel.
2. **`--keep-running` is not an end-of-media signal.** It waits for the IINA *application* to
   exit, not the current file. Playback state must come from mpv IPC.
3. **mpv options are applied per player core.** `AppDelegate.applicationDidFinishLaunching`
   creates a new `PlayerCore`, calls `CommandLineStatus.applyMPVArguments(to:)`
   (`playerCore.mpv.setString`) and only then opens the URL. A unique
   `--mpv-input-ipc-server=<path>` therefore binds to the core that plays our media, so concurrent
   IINA windows cannot be confused with ours.
4. **Launching with no media URL does not work.** With no filename and no stdin, IINA prints
   "This binary is not intended for being used as a command line tool" and returns *before*
   setting `commandLineStatus.isCommandLine`; the mpv arguments are never applied and no socket is
   created. The safer "open an empty player, then send `loadfile` over IPC" sequence proposed in
   the plan is therefore **not available** with official IINA. See "Token exposure" below.
5. **The `iina://` URL scheme cannot set up IPC, so it is not used at all.**
   `AppDelegate.parsePendingURL` only applies `mpv_*` parameters present in
   `AppData.safeMPVOptions`, which includes `start`, `pause`, `user-agent`,
   `http-header-fields`, `aid`, `sid`, `speed`, `volume` — but **not** `input-ipc-server`. A URL
   scheme launch could therefore start playback but never report any of it back, which is not a
   useful mode for this integration. `iina-cli` is the only launcher.
6. **Closing a player window** calls `player.stop()`
   (`MainWindowController.windowWillClose`), which sends mpv `stop`. Expected result: `end-file`
   with reason `stop` on a socket that stays open. Quitting IINA calls `PlayerCore.shutdown()` →
   `mpv.mpvQuit()` → mpv `quit`, i.e. a `shutdown` event and/or socket disconnect. Both are
   mapped to `canceled`.
7. **Launching while IINA is already running still works.** This was the highest-risk assumption,
   because `iina-cli` execs the IINA binary directly rather than going through LaunchServices; had
   macOS routed the launch into the existing instance, `applicationDidFinishLaunching` would not
   have run and no IPC socket would have been created. Consecutive playbacks were observed to
   start and report state correctly, so the assumption holds in practice.
8. **IINA records playback history including the full URL.**
   `PlayerCore` calls `HistoryController.shared.add(url, …)` on file-loaded whenever the
   `recordPlaybackHistory` preference is on (the default). The Jellyfin URL, access token
   included, is written to IINA's own history file.

### Manual verification still required

Against a real IINA install, with the matrix in the implementation plan:

- Whether a fully watched item is still marked played. The stop report carries the last `time-pos`
  the socket delivered, and mpv is known to publish an unhelpful value right at end of file. A null
  `time-pos` is ignored so it cannot zero the position, but a bogus numeric one would be accepted.
  If this turns out to be wrong, the observed `eof-reached` property is the hook for pinning the
  reported position to the duration.

If any of these contradict the design, adjust the integration boundary — an IINA plugin providing
a local bridge, then a minimal plugin, and only as a last resort a fork — rather than patching
IINA itself.

## Token exposure boundary

Jellyfin playback URLs typically embed a long-lived `api_key`. Because official IINA cannot create
the IPC socket before it is given a media URL (finding 4 above), the URL **must** be passed on the
`iina-cli` command line. That means:

- The URL is briefly visible in the local process list (`ps`) to other processes of this user.
- IINA writes the URL to its own playback history and log files. Those are IINA's files, outside
  this application's control. Users who consider this unacceptable can turn off
  "Record playback history" in IINA's settings.

Within this application:

- The media URL, subtitle URLs, HTTP headers and CLI arguments are never written to the log. The
  `iina-cli` process has its stdout and stderr routed to the null device so it cannot echo them
  back. Error reports carry exit codes and enum values only.
- `Log::CensorAuthTokens` additionally redacts `X-Emby-Token`, `Authorization: Bearer` and
  `Token="…"` alongside the existing `api_key` patterns. JavaScript console output is forwarded to
  the application log, so the web adapter logs only a URL's origin.
- No URL is ever written to a settings file.
- The IPC socket is a Unix domain socket under `/tmp/jfd-iina-<pid>/`, a directory created with
  owner-only permissions. No TCP port is opened. The socket is removed when the session ends and
  the directory is removed at shutdown.
- The playback URL is the only URL handed out. Subtitles are selected by track index, so no second
  authenticated URL is ever built.

A local authenticating proxy would remove the command-line exposure entirely, but it has to proxy
Range, HEAD, redirects and HLS manifests correctly; it is deliberately out of scope here.

## Tests

`tests/test_iina.cpp` covers millisecond/second conversion, `iina-cli` argument arrays (each
argv entry separate and unquoted, with the media URL untouched), track selection mapping, that an
external subtitle URL can never reach the command line, socket path length, and the full session
state machine — `end-file` reason mapping, exactly one terminal signal per session, and late
events from a superseded session being ignored.

`tests/test_mpvipc.cpp` covers JSON IPC framing (single, batched, split and malformed messages)
and drives the client against a real `QLocalServer`: delayed socket appearance, connection
timeout, interleaved responses and events, out-of-order request ids, and disconnect.

Neither test requires IINA to be installed.
