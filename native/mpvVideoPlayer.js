/* eslint-disable indent */
(function() {
    function getMediaStreamAudioTracks(mediaSource) {
        return mediaSource.MediaStreams.filter(function (s) {
            return s.Type === 'Audio';
        });
    }

    /**
     * The user's own IINA installation, exposed over the WebChannel. It is the only playback
     * backend in this build: video appears in IINA's own window, so this adapter never creates a
     * video surface, changes backdrop transparency or takes over the OSD.
     */
    function iinaPlayer() {
        return window.api.iinaPlayer;
    }

    class mpvVideoPlayer {
        constructor({ events, loading, appRouter, globalize, appHost, appSettings, playbackManager, confirm }) {
            this.events = events;
            this.loading = loading;
            this.appRouter = appRouter;
            this.globalize = globalize;
            this.appHost = appHost;
            this.appSettings = appSettings;
            this.playbackManager = playbackManager;
            this.confirm = confirm;

            /**
             * @type {string}
             */
            this.name = 'IINA Video Player';
            /**
             * @type {string}
             */
            this.type = 'mediaplayer';
            /**
             * @type {string}
             */
            this.id = 'mpvvideoplayer';
            this.syncPlayWrapAs = 'htmlvideoplayer';
            this.priority = -1;
            this.useFullSubtitleUrls = true;
            /**
             * @type {boolean}
             */
            this.isLocalPlayer = true;
            /**
             * @type {boolean}
             */
            this.isFetching = false;

            /**
             * @type {number | undefined}
             */
            this._subtitleTrackIndexToSetOnPlaying = undefined;
            /**
             * @type {number | null}
             */
            this._audioTrackIndexToSetOnPlaying = undefined;
            /**
             * @type {boolean | undefined}
             */
            this._showTrackOffset = undefined;
            /**
             * @type {number | undefined}
             */
            this._currentTrackOffset = undefined;
            /**
             * @type {string[] | undefined}
             */
            this._supportedFeatures = undefined;
            /**
             * @type {string | undefined}
             */
            this._currentSrc = undefined;
            /**
             * @type {boolean | undefined}
             */
            this._started = undefined;
            /**
             * @type {boolean | undefined}
             */
            this._timeUpdated = undefined;
            /**
             * @type {number | null | undefined}
             */
            this._currentTime = undefined;
            /**
             * @private (used in other files)
             * @type {any | undefined}
             */
            this._currentPlayOptions = undefined;
            /**
             * @type {any | undefined}
             */
            this._lastProfile = undefined;
            /**
             * @type {number | undefined}
             */
            this._duration = undefined;
            /**
             * @type {boolean}
             */
            this._paused = false;
            /**
             * @type {int}
             */
            this._volume = 100;
            /**
             * @type {boolean}
             */
            this._muted = false;
            /**
             * @type {float}
             */
            this._playRate;
            /**
             * @type {boolean}
             */
            this._hasConnection = false;
            /**
             * @type {Array<{start: number, end: number}>}
             */
            this._bufferedRanges = [];
            /**
             * Guards against reporting 'stopped' twice for one playback, which can otherwise
             * happen when an explicit stop() is followed by the backend's own terminal signal.
             * @type {boolean}
             */
            this._stopReported = true;

            /**
             * @private
             */
            this.onBufferedRangesUpdated = (ranges) => {
                this._bufferedRanges = ranges;
            };

            /**
             * @private
             */
            this.onEnded = () => {
                // Finishing an item returns to the Jellyfin UI rather than opening a new IINA
                // window for the next one.
                this.stopWithoutAdvancing();
            };

            /**
             * @private
             * Playback ended without finishing: the user stopped it, or closed the external
             * player's window.
             */
            this.onCanceled = () => {
                this.stopWithoutAdvancing();
            };

            /**
             * @private
             */
            this.onTimeUpdate = (time) => {
                if (time && !this._timeUpdated) {
                    this._timeUpdated = true;
                }

                this._currentTime = time;
                this.events.trigger(this, 'timeupdate');
            };

            /**
             * @private
             */
            this.onPlaying = () => {
                if (!this._started) {
                    this._started = true;

                    this.loading.hide();

                    const volume = this.getSavedVolume() * 100;
                    this.setVolume(volume, false);

                    this.setPlaybackRate(this.getPlaybackRate());
                }

                if (this._paused) {
                    this._paused = false;
                    this.events.trigger(this, 'unpause');
                }

                this.events.trigger(this, 'playing');
            };

            /**
             * @private
             */
            this.onPause = () => {
                this._paused = true;
                // For Syncplay ready notification
                this.events.trigger(this, 'pause');
            };

            this.onWaiting = () => {
                this.events.trigger(this, 'waiting');
            };

            /**
             * @private
             * @param e {Event} The event received from the `<video>` element
             */
            this.onError = async (error) => {
                this.removeMediaDialog();
                console.error(`media error: ${error}`);

                const errorData = {
                    type: 'mediadecodeerror'
                };

                try {
                    await confirm({
                        title: "Playback Failed",
                        text: `Playback failed with error "${error}". Retry with transcode? (Note this may hang the player.)`,
                        cancelText: "Cancel",
                        confirmText: "Retry"
                    });
                } catch (ex) {
                    // User declined retry
                    errorData.streamInfo = {
                        // Prevent jellyfin-web retrying with transcode
                        // which crashes the player
                        mediaSource: {
                            SupportsTranscoding: false
                        }
                    };
                }

                this.events.trigger(this, 'error', [errorData]);
            };

            this.onDuration = (duration) => {
                this._duration = duration;
            };
        }

        currentSrc() {
            return this._currentSrc;
        }

        async play(options) {
            const backend = iinaPlayer();
            if (backend.available === false) {
                // No silent fallback: there is no HTML5 player in this fork, and quietly using the
                // built-in renderer would contradict the user's macOS configuration.
                this.loading.hide();
                try {
                    await this.confirm({
                        title: 'IINA Is Required',
                        text: 'Video playback on macOS uses the IINA application, which does not appear to be installed. Install IINA and try again.',
                        cancelText: 'Cancel',
                        confirmText: 'Download IINA'
                    });
                    backend.openDownloadPage();
                } catch (ex) {
                    // User dismissed the prompt.
                }
                throw new Error('IINA is not installed');
            }

            this._started = false;
            this._timeUpdated = false;
            this._currentTime = null;
            this._bufferedRanges = [];
            this._stopReported = false;

            this.resetSubtitleOffset();
            if (options.fullscreen) {
                this.loading.show();
            }
            this.connectBackendSignals(iinaPlayer());
            return await this.setCurrentSrc(options);
        }

        getSavedVolume() {
            return this.appSettings.get('volume') || 1;
        }


        tryGetFramerate(options) {
            if (options.mediaSource && options.mediaSource.MediaStreams) {
                for (let stream of options.mediaSource.MediaStreams) {
                    if (stream.Type == "Video") {
                        return stream.RealFrameRate || stream.AverageFrameRate || null;
                    }
                }
            }
        }

        /**
         * @private
         */
        getRelativeIndexByType(mediaStreams, jellyIndex, streamType) {
            let relIndex = 1;
            for (const source of mediaStreams) {
                if (source.Type != streamType || source.IsExternal) {
                    continue;
                }
                if (source.Index == jellyIndex) {
                    return relIndex;
                }
                relIndex += 1;
            }
            return null;
        }

        /**
         * @private
         */
        setCurrentSrc(options) {
            return new Promise((resolve) => {
                const val = options.url;
                this._currentSrc = val;
                // The URL carries an access token and console messages are forwarded to the
                // application log, so only the origin is recorded.
                console.debug(`playing url from: ${new URL(val, window.location.href).origin}`);

                // Convert to seconds
                const ms = (options.playerStartPositionTicks || 0) / 10000;
                this._currentPlayOptions = options;
                this._subtitleTrackIndexToSetOnPlaying = options.mediaSource.DefaultSubtitleStreamIndex == null ? -1 : options.mediaSource.DefaultSubtitleStreamIndex;
                this._audioTrackIndexToSetOnPlaying = options.mediaSource.DefaultAudioStreamIndex;

                console.log('[MPV] Audio track index:', this._audioTrackIndexToSetOnPlaying);
                console.log('[MPV] Subtitle track index:', this._subtitleTrackIndexToSetOnPlaying);

                const streamdata = {type: 'video', headers: {'User-Agent': jmpInfo.userAgent}, metadata: options.item, media: {}};
                const fps = this.tryGetFramerate(options);
                if (fps) {
                    streamdata.frameRate = fps;
                }

                const player = iinaPlayer();

                const streams = options.mediaSource?.MediaStreams || [];

                // Handle audio
                const audioRelIndex = this._audioTrackIndexToSetOnPlaying != null && this._audioTrackIndexToSetOnPlaying >= 0
                    ? this.getRelativeIndexByType(streams, this._audioTrackIndexToSetOnPlaying, 'Audio')
                    : 1;

                // Direct Play delivers the original file, so every subtitle the server reports is
                // physically present in the container. Selecting it by track index avoids a second
                // authenticated request for a stream we already have. Sidecar subtitle files are
                // out of scope; see docs/iina-external-player.md.
                let subtitleParam = -1;
                if (this._subtitleTrackIndexToSetOnPlaying >= 0) {
                    const relIndex = this.getRelativeIndexByType(streams, this._subtitleTrackIndexToSetOnPlaying, 'Subtitle');
                    subtitleParam = relIndex != null ? relIndex : -1;
                    console.log('[MPV] Mapped subtitle index:', this._subtitleTrackIndexToSetOnPlaying, '->', subtitleParam);
                }

                console.log('[MPV] Mapped audio index:', this._audioTrackIndexToSetOnPlaying, '->', audioRelIndex);

                player.load(val,
                    { startMilliseconds: ms, autoplay: true },
                    streamdata,
                    audioRelIndex,
                    subtitleParam,
                    resolve);
            });
        }

        setSubtitleStreamIndex(index) {
            console.log('[MPV] setSubtitleStreamIndex called with index:', index);
            this._subtitleTrackIndexToSetOnPlaying = index;

            const player = iinaPlayer();

            if (index < 0) {
                player.setSubtitleStream(-1);
                return;
            }

            const streams = this._currentPlayOptions?.mediaSource?.MediaStreams || [];
            const relIndex = this.getRelativeIndexByType(streams, index, 'Subtitle');
            console.log('[MPV] Mapped subtitle index:', index, '->', relIndex);
            player.setSubtitleStream(relIndex != null ? relIndex : -1);
        }

        setSecondarySubtitleStreamIndex(index) {
            // MPV doesn't support secondary subtitles - no-op for compatibility
            console.log('[MPV] setSecondarySubtitleStreamIndex not supported, ignoring index:', index);
        }

        resetSubtitleOffset() {
            this._currentTrackOffset = 0;
            this._showTrackOffset = false;
            iinaPlayer().setSubtitleDelay(0);
        }

        enableShowingSubtitleOffset() {
            this._showTrackOffset = true;
        }

        disableShowingSubtitleOffset() {
            this._showTrackOffset = false;
        }

        isShowingSubtitleOffsetEnabled() {
            return this._showTrackOffset;
        }

        setSubtitleOffset(offset) {
            const offsetValue = parseFloat(offset);
            this._currentTrackOffset = offsetValue;
            iinaPlayer().setSubtitleDelay(Math.round(offsetValue * 1000));
        }

        getSubtitleOffset() {
            return this._currentTrackOffset;
        }

        /**
         * @private
         */
        isAudioStreamSupported() {
            return true;
        }

        /**
         * @private
         */
        getSupportedAudioStreams() {
            const profile = this._lastProfile;

            return getMediaStreamAudioTracks(this._currentPlayOptions.mediaSource).filter((stream) => {
                return this.isAudioStreamSupported(stream, profile);
            });
        }

        setAudioStreamIndex(index) {
            console.log('[MPV] setAudioStreamIndex called with index:', index);
            this._audioTrackIndexToSetOnPlaying = index;

            const streams = this._currentPlayOptions?.mediaSource?.MediaStreams || [];
            const relIndex = index < 0 ? -1 : this.getRelativeIndexByType(streams, index, 'Audio');
            console.log('[MPV] Mapped audio index:', index, '->', relIndex);
            iinaPlayer().setAudioStream(relIndex != null ? relIndex : -1);
        }

        onEndedInternal() {
            if (this._stopReported) {
                return;
            }
            this._stopReported = true;

            const stopInfo = {
                src: this._currentSrc
            };

            this.events.trigger(this, 'stopped', [stopInfo]);

            this._currentTime = null;
            this._currentSrc = null;
            this._currentPlayOptions = null;
        }

        /**
         * @private
         * Ends playback without letting playbackManager queue up the next item.
         *
         * playbackManager only clears its _playNextAfterEnded flag inside its own stop(); a bare
         * 'stopped' event is indistinguishable from media ending naturally and makes it start the
         * next episode. With an external player that means a new IINA window opens the moment the
         * current one is closed or finishes, which loops for as long as the queue has items.
         */
        stopWithoutAdvancing() {
            if (this._stopReported) {
                return;
            }

            if (this.playbackManager) {
                this.playbackManager.stop(this);
            } else {
                // Older web clients may not inject playbackManager. Leaving the playing state
                // matters more than suppressing the next item.
                this.onEndedInternal();
            }
        }

        stop(destroyPlayer) {
            iinaPlayer().stop();

            this.onEndedInternal();

            if (destroyPlayer) {
                this.destroy();
            }
            return Promise.resolve();
        }

        /**
         * @private
         * Nothing is drawn in this window, so ending playback is just stopping IINA.
         */
        removeMediaDialog() {
            iinaPlayer().stop();
        }

        /**
         * @private
         * Connects the backend signals this adapter translates into Jellyfin Web events. Kept
         * symmetric with disconnectBackendSignals() so repeated playback does not accumulate
         * handlers.
         */
        connectBackendSignals(player) {
            if (this._hasConnection) {
                return;
            }
            this._hasConnection = true;

            player.playing.connect(this.onPlaying);
            player.positionUpdate.connect(this.onTimeUpdate);
            player.finished.connect(this.onEnded);
            player.updateDuration.connect(this.onDuration);
            player.error.connect(this.onError);
            player.paused.connect(this.onPause);
            player.bufferedRangesUpdated.connect(this.onBufferedRangesUpdated);
            // IINA can be stopped or closed outside this window; without this Jellyfin would stay
            // in the playing state forever.
            player.canceled.connect(this.onCanceled);
        }

        /**
         * @private
         */
        disconnectBackendSignals(player) {
            if (!this._hasConnection) {
                return;
            }
            this._hasConnection = false;

            player.playing.disconnect(this.onPlaying);
            player.positionUpdate.disconnect(this.onTimeUpdate);
            player.finished.disconnect(this.onEnded);
            player.updateDuration.disconnect(this.onDuration);
            player.error.disconnect(this.onError);
            player.paused.disconnect(this.onPause);
            player.bufferedRangesUpdated.disconnect(this.onBufferedRangesUpdated);
            player.canceled.disconnect(this.onCanceled);
        }

        destroy() {
            this.removeMediaDialog();

            this._bufferedRanges = [];
            this._duration = undefined;
            this.disconnectBackendSignals(iinaPlayer());
        }

    /**
     * @private
     */
    canPlayMediaType(mediaType) {
        return (mediaType || '').toLowerCase() === 'video';
    }

    canPlayItem(item, playOptions) {
        // Delegate to canPlayMediaType - MPV can play any video the media type check passes
        return this.canPlayMediaType(item.MediaType);
    }

    /**
     * @private
     */
    supportsPlayMethod() {
        return true;
    }

    /**
     * @private
     */
    getDeviceProfile(item, options) {
        if (this.appHost.getDeviceProfile) {
            return this.appHost.getDeviceProfile(item, options);
        }

        return Promise.resolve({});
    }

    /**
     * @private
     */
    static getSupportedFeatures() {
        return ['PlaybackRate', 'SetAspectRatio'];
    }

    supports(feature) {
        if (!this._supportedFeatures) {
            this._supportedFeatures = mpvVideoPlayer.getSupportedFeatures();
        }

        return this._supportedFeatures.includes(feature);
    }

    isFullscreen() {
        // Check native window fullscreen state
        if (window.jmpInfo && window.jmpInfo.settings && window.jmpInfo.settings.main) {
            return window.jmpInfo.settings.main.fullscreen === true;
        }
        return false;
    }

    toggleFullscreen() {
        if (window.api && window.api.input) {
            window.api.input.executeActions(['host:fullscreen']);
        }
    }

    // Save this for when playback stops, because querying the time at that point might return 0
    currentTime(val) {
        if (val != null) {
            iinaPlayer().seekTo(val);
            return;
        }

        return this._currentTime;
    }

    currentTimeAsync() {
        return new Promise((resolve) => {
            iinaPlayer().getPosition(resolve);
        });
    }

    duration() {
        if (this._duration) {
            return this._duration;
        }

        return null;
    }

    canSetAudioStreamIndex() {
        return true;
    }

    static onPictureInPictureError(err) {
        console.error(`Picture in picture error: ${err}`);
    }

    setPictureInPictureEnabled() {}

    isPictureInPictureEnabled() {
        return false;
    }

    isAirPlayEnabled() {
        return false;
    }

    setAirPlayEnabled() {}

    setBrightness() {}

    getBrightness() {
        return 100;
    }

    seekable() {
        return Boolean(this._duration);
    }

    pause() {
        iinaPlayer().pause();
    }

    // This is a retry after error
    resume() {
        this._paused = false;
        iinaPlayer().play();
    }

    unpause() {
        iinaPlayer().play();
    }

    paused() {
        return this._paused;
    }

    setPlaybackRate(value) {
        let playSpeed = +value; //this comes as a string from player force int for now
        this._playRate = playSpeed;
        iinaPlayer().setPlaybackRate(playSpeed * 1000);
    }

    getPlaybackRate() {
        if(!this._playRate) //On startup grab default
        {
            let playRate = window.jmpInfo.settings.video.default_playback_speed;

            if(!playRate) //fallback if default missing
                playRate = 1;

            this._playRate = playRate;
        }
        return this._playRate;
    }

    getSupportedPlaybackRates() {
        return [{
            name: '0.5x',
            id: 0.5
        }, {
            name: '0.75x',
            id: 0.75
        }, {
            name: '1x',
            id: 1.0
        }, {
            name: '1.25x',
            id: 1.25
        }, {
            name: '1.5x',
            id: 1.5
        }, {
            name: '1.75x',
            id: 1.75
        }, {
            name: '2x',
            id: 2.0
        }, {
            name: '2.5x',
            id: 2.5
        }, {
            name: '3x',
            id: 3.0
        }, {
            name: '3.5x',
            id: 3.5
        }, {
            name: '4.0x',
            id: 4.0
        }];
    }

    saveVolume(value) {
        if (value) {
            this.appSettings.set('volume', value);
        }
    }

    setVolume(val, save = true) {
        val = Number(val);
        if (!isNaN(val)) {
            this._volume = val;
            if (save) {
                this.saveVolume(val / 100);
                this.events.trigger(this, 'volumechange');
            }
            iinaPlayer().setVolume(val);
        }
    }

    getVolume() {
        return this._volume;
    }

    volumeUp() {
        this.setVolume(Math.min(this.getVolume() + 2, 100));
    }

    volumeDown() {
        this.setVolume(Math.max(this.getVolume() - 2, 0));
    }

    setMute(mute, triggerEvent = true) {
        this._muted = mute;
        iinaPlayer().setMuted(mute);
        if (triggerEvent) {
            this.events.trigger(this, 'volumechange');
        }
    }

    isMuted() {
        return this._muted;
    }

    togglePictureInPicture() {
    }

    toggleAirPlay() {
    }

    getBufferedRanges() {
        return this._bufferedRanges;
    }

    getStats() {
        const playOptions = this._currentPlayOptions || [];
        const categories = [];

        if (!this._currentPlayOptions) {
            return Promise.resolve({
                categories: categories
            });
        }

        const mediaCategory = {
            stats: [],
            type: 'media'
        };
        categories.push(mediaCategory);

        if (playOptions.url) {
            //  create an anchor element (note: no need to append this element to the document)
            let link = document.createElement('a');
            //  set href to any path
            link.setAttribute('href', playOptions.url);
            const protocol = (link.protocol || '').replace(':', '');

            if (protocol) {
                mediaCategory.stats.push({
                    label: this.globalize.translate('LabelProtocol'),
                    value: protocol
                });
            }

            link = null;
        }

        mediaCategory.stats.push({
            label: this.globalize.translate('LabelStreamType'),
            value: 'Video'
        });

        const videoCategory = {
            stats: [],
            type: 'video'
        };
        categories.push(videoCategory);

        const audioCategory = {
            stats: [],
            type: 'audio'
        };
        categories.push(audioCategory);

        return Promise.resolve({
            categories: categories
        });
    }

    getSupportedAspectRatios() {
        const options = window.jmpInfo.settingsDescriptions.video.find(x => x.key == 'aspect').options;
        const current = window.jmpInfo.settings.video.aspect;

        const getOptionName = (option) => {
            const canTranslate = {
                'normal': 'Auto',
                'zoom': 'AspectRatioCover',
                'stretch': 'AspectRatioFill',
            }
            const name = option.replace('video.aspect.', '');
            return canTranslate[name]
                ? this.globalize.translate(canTranslate[name])
                : name;
        }

        return options.map(x => ({
            id: x.value,
            name: getOptionName(x.title),
            selected: x.value == current
        }));
    }

    getAspectRatio() {
        return window.jmpInfo.settings.video.aspect;
    }

    setAspectRatio(value) {
        window.jmpInfo.settings.video.aspect = value;
    }
}
/* eslint-enable indent */

window._mpvVideoPlayer = mpvVideoPlayer;
})();
