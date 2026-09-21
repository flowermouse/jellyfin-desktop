(function() {
const remap = {
    "play_pause": "playpause",
    "seek_forward": "fastforward",
    "seek_backward": "rewind",
    "host:fullscreen": "togglefullscreen",
    "cycle_audio": "changeaudiotrack",
    "cycle_subtitles": "changesubtitletrack",
    "increase_volume": "volumeup",
    "decrease_volume": "volumedown",
    "step_backward": "previouschapter",
    "step_forward": "nextchapter",
    "enter": "select",
}

class inputPlugin {
    constructor({ inputManager, playbackManager }) {
        this.name = 'Input Plugin';
        this.type = 'input';
        this.id = 'inputPlugin';

        (async () => {
            const api = await window.apiPromise;

            api.input.hostInput.connect((actions) => {
                actions.forEach(action => {
                    if (action === 'shuffle') {
                        playbackManager.setQueueShuffleMode('Shuffle');
                    } else if (action === 'sorted') {
                        playbackManager.setQueueShuffleMode('Sorted');
                    } else if (action === 'previous') {
                        const currentPlayer = playbackManager._currentPlayer;
                        if (currentPlayer && playbackManager.isPlayingAudio(currentPlayer)) {
                            const currentTime = playbackManager.currentTime(currentPlayer);
                            const currentIndex = playbackManager.getCurrentPlaylistIndex(currentPlayer);

                            if (currentTime >= 5 * 1000 || currentIndex <= 0) {
                                playbackManager.seekPercent(0, currentPlayer);
                            } else {
                                playbackManager.previousTrack(currentPlayer);
                            }
                        } else if (currentPlayer) {
                            playbackManager.previousTrack(currentPlayer);
                        }
                    } else {
                        if (remap.hasOwnProperty(action)) {
                            action = remap[action];
                        }
                        inputManager.handleCommand(action, {});
                    }
                });
            });

            let lastFullscreenState = window.jmpInfo.settings.main.fullscreen;
            window.jmpInfo.settingsUpdate.push(function(section) {
                if (section === 'main') {
                    const currentFullscreenState = window.jmpInfo.settings.main.fullscreen;
                    if (currentFullscreenState !== lastFullscreenState) {
                        lastFullscreenState = currentFullscreenState;
                        const currentPlayer = playbackManager._currentPlayer;
                        if (currentPlayer) {
                            window.Events.trigger(currentPlayer, 'fullscreenchange');
                        }
                    }
                }
            });

            // Playback control coming from the host side. Nothing reports playback state back:
            // video runs in IINA, which owns its own Now Playing integration.
            api.input.volumeChanged.connect(function(volume) {
                const currentPlayer = playbackManager._currentPlayer;
                if (currentPlayer && typeof currentPlayer.setVolume === 'function') {
                    currentPlayer.setVolume(volume);
                }
            });

            api.input.rateChanged.connect(function(rate) {
                const currentPlayer = playbackManager._currentPlayer;
                if (currentPlayer && typeof currentPlayer.setPlaybackRate === 'function') {
                    currentPlayer.setPlaybackRate(rate);
                }
            });

            api.input.positionSeek.connect(function(positionMs) {
                const currentPlayer = playbackManager._currentPlayer;
                if (currentPlayer) {
                    const duration = playbackManager.duration();
                    if (duration) {
                        const percent = (positionMs * 10000) / duration * 100;
                        playbackManager.seekPercent(percent, currentPlayer);
                    }
                }
            });

            api.system.hello("jmpInputPlugin");
        })();
    }
}

window._inputPlugin = inputPlugin;
})();
