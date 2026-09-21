// Exercises the injected web-layer scripts against a stubbed native bridge.
//
// These scripts are loaded into a remote Jellyfin Web page, so nothing type-checks them and a
// reference to a WebChannel object that no longer exists only surfaces as a runtime TypeError
// inside jellyfin-web's event dispatch - where it looks like a generic playback failure. This
// test walks a whole playback lifecycle so that class of breakage fails here instead.
//
// Run: node tests/js/test_adapters.js   (also registered with CTest as test_js_adapters)

const fs = require('fs');
const path = require('path');

const NATIVE_DIR = path.join(__dirname, '..', '..', 'native');

let failures = 0;
function check(condition, description) {
  if (condition) {
    console.log(`PASS   : ${description}`);
  } else {
    console.log(`FAIL!  : ${description}`);
    failures++;
  }
}

function equal(actual, expected, description) {
  check(actual === expected, `${description} (got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)})`);
}

// A WebChannel signal: connect/disconnect must stay balanced, so disconnecting a handler that
// was never connected is an error rather than a silent no-op.
function makeSignal(name) {
  const handlers = [];
  return {
    connect: (h) => handlers.push(h),
    disconnect: (h) => {
      const i = handlers.indexOf(h);
      if (i < 0) throw new Error(`disconnect of a handler that was never connected: ${name}`);
      handlers.splice(i, 1);
    },
    fire: (...args) => handlers.slice().forEach((h) => h(...args)),
    connectionCount: () => handlers.length,
  };
}

const SIGNALS = ['playing', 'positionUpdate', 'finished', 'updateDuration', 'error', 'paused',
                 'bufferedRangesUpdated', 'canceled'];
const METHODS = ['load', 'play', 'pause', 'stop', 'seekTo', 'setVolume', 'setMuted',
                 'setPlaybackRate', 'setAudioStream', 'setSubtitleStream', 'setSubtitleDelay',
                 'getPosition', 'getDuration', 'openDownloadPage'];

function makeEnvironment() {
  const nativeCalls = [];
  const signals = {};
  for (const name of SIGNALS) signals[name] = makeSignal(name);

  const iinaPlayer = Object.assign({ externalPlayback: true, available: true }, signals);
  for (const method of METHODS) {
    iinaPlayer[method] = (...args) => {
      nativeCalls.push(method);
      // load() and getPosition() answer through a WebChannel callback.
      if (method === 'load') args[args.length - 1](true);
      if (method === 'getPosition') args[0](1234);
    };
  }

  const win = {
    location: { href: 'http://server:8096/web/index.html' },
    api: { iinaPlayer, system: { hello() {} }, input: {} },
    jmpInfo: {
      userAgent: 'Jellyfin Desktop test',
      settingsUpdate: [],
      settings: { video: { default_playback_speed: 1, aspect: 'normal' }, main: { fullscreen: true } },
    },
  };

  return { win, iinaPlayer, signals, nativeCalls };
}

function loadScript(env, name) {
  const source = fs.readFileSync(path.join(NATIVE_DIR, name), 'utf8');
  const sandbox = { window: env.win, jmpInfo: env.win.jmpInfo, console: { log() {}, debug() {}, error() {}, warn() {} } };
  // eslint-disable-next-line no-new-func
  new Function('window', 'jmpInfo', 'console', source)(sandbox.window, sandbox.jmpInfo, sandbox.console);
}

function playOptions() {
  return {
    url: 'https://server:8096/Videos/1/stream.mkv?api_key=0123456789abcdef',
    playerStartPositionTicks: 600000000, // 60s
    fullscreen: true,
    item: {},
    mediaSource: {
      DefaultAudioStreamIndex: 1,
      DefaultSubtitleStreamIndex: 2,
      MediaStreams: [
        { Index: 0, Type: 'Video' },
        { Index: 1, Type: 'Audio' },
        // Reported as External by the server, but present in the container under Direct Play.
        { Index: 2, Type: 'Subtitle', DeliveryMethod: 'External', DeliveryUrl: '/Subtitles/1?api_key=0123456789abcdef' },
      ],
    },
  };
}

async function testVideoAdapter() {
  const env = makeEnvironment();
  loadScript(env, 'mpvVideoPlayer.js');

  const triggered = [];
  let stopRequests = 0;
  const player = new env.win._mpvVideoPlayer({
    events: { trigger: (_, name) => triggered.push(name) },
    loading: { show() {}, hide() {} },
    appRouter: { showVideoOsd() { throw new Error('must not navigate to the local video OSD'); } },
    globalize: { translate: (k) => k },
    appHost: {},
    appSettings: { get: () => 1, set() {} },
    playbackManager: { stop(p) { stopRequests++; return p.stop(true, true); } },
    confirm: async () => { throw new Error('dismissed'); },
  });

  await player.play(playOptions());

  check(!env.nativeCalls.includes('setVideoRectangle'),
        'play() does not touch the local video surface');
  check(env.nativeCalls.includes('load'), 'play() loads the media in IINA');

  env.signals.playing.fire();
  env.signals.updateDuration.fire(3600000);
  env.signals.positionUpdate.fire(61000);
  env.signals.paused.fire();
  env.signals.playing.fire();

  equal(triggered.join(','), 'playing,timeupdate,pause,unpause,playing',
        'backend signals translate into Jellyfin Web events');
  equal(player.currentTime(), 61000, 'position is reported in milliseconds');
  equal(player.duration(), 3600000, 'duration is reported in milliseconds');

  // The subtitle is embedded even though the server calls it External, so it is selected by
  // track index rather than fetched again over an authenticated URL.
  const subtitleCalls = [];
  env.iinaPlayer.setSubtitleStream = (arg) => subtitleCalls.push(arg);
  player.setSubtitleStreamIndex(2);
  equal(subtitleCalls[0], 1, 'an embedded subtitle is selected by relative track index');

  triggered.length = 0;
  env.signals.finished.fire();
  equal(stopRequests, 1, 'finishing goes through playbackManager.stop() so the queue does not advance');
  equal(triggered.join(','), 'stopped', 'finishing reports stopped exactly once');

  triggered.length = 0;
  env.signals.canceled.fire();
  equal(triggered.join(','), '', 'a late canceled after finishing reports nothing');

  const leaked = SIGNALS.filter((n) => env.signals[n].connectionCount() > 0);
  equal(leaked.join(','), '', 'destroy() disconnects every signal it connected');

  // A second playback has to reconnect cleanly rather than run on stale handlers.
  await player.play(playOptions());
  triggered.length = 0;
  env.signals.canceled.fire();
  equal(triggered.join(','), 'stopped', 'closing IINA during a later session still reports stopped');
  equal(stopRequests, 2, 'closing IINA also suppresses the next item');
}

async function testInputPlugin() {
  const env = makeEnvironment();

  // Every signal the plugin subscribes to, and nothing else: an unknown property access blows up.
  const hostSignals = {};
  for (const name of ['hostInput', 'volumeChanged', 'rateChanged', 'positionSeek']) {
    hostSignals[name] = makeSignal(name);
  }
  env.win.api.input = hostSignals;
  env.win.apiPromise = Promise.resolve(env.win.api);
  env.win.Events = { trigger() {}, on() {}, off() {} };

  loadScript(env, 'inputPlugin.js');

  const handled = [];
  // eslint-disable-next-line no-new
  new env.win._inputPlugin({
    inputManager: { handleCommand: (c) => handled.push(c) },
    playbackManager: { _currentPlayer: null, duration: () => 0, setQueueShuffleMode() {} },
  });

  await Promise.resolve();
  await Promise.resolve();

  check(hostSignals.hostInput.connectionCount() === 1, 'the input plugin subscribes to host input');

  // This is the path that used to throw: it ran while no native player object existed.
  hostSignals.hostInput.fire(['play_pause']);
  equal(handled.join(','), 'playpause', 'host actions are remapped onto web-client commands');

  hostSignals.volumeChanged.fire(50);
  hostSignals.rateChanged.fire(1.5);
  hostSignals.positionSeek.fire(1000);
  check(true, 'host volume, rate and seek signals are handled without a native player');
}

(async () => {
  console.log('********* Start testing of Adapters *********');
  await testVideoAdapter();
  await testInputPlugin();
  console.log(`Totals: ${failures} failed`);
  console.log('********* Finished testing of Adapters *********');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
  console.log(`FAIL!  : unexpected exception: ${e.stack}`);
  process.exit(1);
});
