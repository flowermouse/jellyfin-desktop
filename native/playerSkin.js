/* eslint-disable indent */
(function() {
    /**
     * Restyles the parts of jellyfin-web that this application puts in front of the user during
     * playback: the transport slider, the popup menus reached from the OSD, and the motion of
     * both.
     *
     * It is a stylesheet rather than a fork of jellyfin-web because the web client is served by
     * whichever Jellyfin the user connects to. Everything here therefore overrides by specificity
     * and leaves the markup alone, so a client that renames a class loses the restyling instead of
     * breaking.
     *
     * Two easing curves are used throughout. `--jmp-ease` decelerates hard and is what macOS uses
     * for things that appear; `--jmp-ease-quick` is for state a pointer is already touching, where
     * a long tail reads as lag.
     */
    const SKIN_CSS = `
:root {
    --jmp-ease: cubic-bezier(0.32, 0.72, 0, 1);
    --jmp-ease-quick: cubic-bezier(0.4, 0, 0.2, 1);
    --jmp-surface: rgba(30, 30, 32, 0.72);
    --jmp-surface-border: rgba(255, 255, 255, 0.09);
    --jmp-surface-shadow: 0 12px 40px rgba(0, 0, 0, 0.55), 0 1px 0 rgba(255, 255, 255, 0.06) inset;
    --jmp-radius: 12px;
}

/* ---------------------------------------------------------------- transport slider ---------- */

/* The track is 0.2em by default, which at a normal window size is two hairlines. Thickening it
   and growing it further under the pointer gives the drag a target worth aiming at. */
.mdl-slider-background-flex {
    height: 0.32em;
    margin-top: -0.16em;
    border-radius: 0.16em;
    background: rgba(255, 255, 255, 0.22);
    transition: height 0.18s var(--jmp-ease), margin-top 0.18s var(--jmp-ease);
}

.mdl-slider-container:hover .mdl-slider-background-flex,
.mdl-slider-container.jmp-dragging .mdl-slider-background-flex {
    height: 0.5em;
    margin-top: -0.25em;
}

.mdl-slider-background-lower,
.mdl-slider-background-upper {
    border-radius: inherit;
}

/* The buffered range sits between the track and the played range, so it needs to read as a
   distinct third value rather than "slightly brighter grey". */
.mdl-slider-background-upper {
    background: rgba(255, 255, 255, 0.28);
}

/* The played range is driven straight off mpv's playback-time, which reports every frame, so it
   already advances smoothly and must not be given a transition: anything here would make it lag
   the pointer during a drag. */

/* A thumb the size of the track reads as part of it. Keeping it small until the pointer arrives,
   then overshooting slightly, is what gives the grab its snap. */
.mdl-slider::-webkit-slider-thumb {
    height: 0.85em;
    width: 0.85em;
    box-shadow: 0 1px 4px rgba(0, 0, 0, 0.45);
    transition: transform 0.22s var(--jmp-ease), box-shadow 0.22s var(--jmp-ease);
}

.mdl-slider-hoverthumb:hover::-webkit-slider-thumb,
.mdl-slider.show-focus:focus::-webkit-slider-thumb {
    transform: scale(1.45);
    box-shadow: 0 2px 10px rgba(0, 0, 0, 0.55);
}

.mdl-slider-container.jmp-dragging .mdl-slider::-webkit-slider-thumb {
    transform: scale(1.7);
}

/* ------------------------------------------------------------------- seek bubble ------------- */

/* The default bubble is an unrounded #282828 rectangle that pops in at full size. */
.sliderBubble {
    background: var(--jmp-surface);
    -webkit-backdrop-filter: blur(24px) saturate(180%);
    backdrop-filter: blur(24px) saturate(180%);
    border: 1px solid var(--jmp-surface-border);
    border-radius: 8px;
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.5);
    font-variant-numeric: tabular-nums;
    opacity: 0;
    transition: opacity 0.16s var(--jmp-ease), transform 0.16s var(--jmp-ease);
    transform: translate3d(-50%, -100%, 0) scale(0.92);
}

.sliderBubble:not(.hide) {
    opacity: 1;
    transform: translate3d(-50%, -120%, 0) scale(1);
}

.sliderBubbleText {
    padding: 0.35em 0.7em;
    font-variant-numeric: tabular-nums;
}

/* Chapter ticks sit on the track, so they have to match its new height. */
.sliderMarker {
    border-radius: 1px;
}

/* ------------------------------------------------------------------- popup menus ------------- */

/* The menus behind the subtitle, audio and settings buttons. Jellyfin renders them as square,
   fully opaque sheets with 0.1em corners; over video that reads as a dialog box dropped on top
   rather than a layer belonging to the player. */
.actionSheet {
    border-radius: var(--jmp-radius) !important;
    background: var(--jmp-surface) !important;
    -webkit-backdrop-filter: blur(30px) saturate(180%);
    backdrop-filter: blur(30px) saturate(180%);
    border: 1px solid var(--jmp-surface-border) !important;
    box-shadow: var(--jmp-surface-shadow);
    overflow: hidden;
}

.actionsheet-fullscreen {
    border-radius: var(--jmp-radius) !important;
}

.actionSheetContent {
    padding: 0.35em !important;
}

/* Rounded rows that light up under the pointer, instead of full-bleed rectangles. */
.actionSheetMenuItem {
    border-radius: 8px;
    transition: background-color 0.14s var(--jmp-ease-quick);
}

.actionSheetMenuItem:hover,
.actionSheetMenuItem:focus {
    background-color: rgba(255, 255, 255, 0.1);
}

.actionSheetMenuItem:active {
    background-color: rgba(255, 255, 255, 0.16);
}

.actionsheetDivider {
    background: rgba(255, 255, 255, 0.1);
    margin: 0.3em 0.6em;
}

.actionSheetTitle {
    opacity: 0.55;
    font-size: 92%;
    letter-spacing: 0.02em;
}

/* The aside text is the current value of a setting; 5em of margin pushes it off on narrow
   menus and makes the row read as two unrelated columns. */
[dir="ltr"] .actionSheetItemAsideText { margin-left: 2em; }
[dir="rtl"] .actionSheetItemAsideText { margin-right: 2em; }

/* ---------------------------------------------------------------- control bar ---------------- */

/* Jellyfin's scrim is a two-stop linear gradient. A linear alpha ramp over the height of a
   desktop window bands visibly; the extra stops approximate an ease-out so the fade reads as
   smooth, and the bottom is darkened a little for legibility over bright footage. */
.videoOsdBottom {
    background: linear-gradient(0deg,
        rgba(10, 10, 12, 0.88) 0%,
        rgba(10, 10, 12, 0.74) 20%,
        rgba(10, 10, 12, 0.46) 45%,
        rgba(10, 10, 12, 0.18) 70%,
        rgba(10, 10, 12, 0) 100%);
}

.skinHeader-withBackground.osdHeader {
    background: linear-gradient(180deg,
        rgba(10, 10, 12, 0.8) 0%,
        rgba(10, 10, 12, 0.46) 45%,
        rgba(10, 10, 12, 0) 100%);
}

/* The transport cluster is what the eye should land on, so it goes in the middle of the bar.
   Jellyfin puts \`margin-right: auto\` on .osdTimeText, which shoves every control after it to
   the right edge and leaves the centre of a wide window empty with the transport stranded in
   the left corner.

   Centring is done by taking the cluster out of flow rather than by juggling auto margins,
   because the groups either side of it have very different widths - "Ends at 12:54" against six
   buttons and a volume slider - so any flow-based centring lands visibly off-centre.

   Only above 60em: Jellyfin starts dropping controls at 50em and below, and once the row is
   crowded an out-of-flow cluster would sit on top of its neighbours. */
@media all and (min-width: 60em) {
    .videoOsdBottom .buttons {
        position: relative;
        min-height: 2.9em;
    }

    .videoOsdBottom .buttons > div[dir="ltr"] {
        position: absolute;
        left: 50%;
        transform: translateX(-50%);
        display: flex;
        align-items: center;
    }
}

/* Play/pause is the one control worth distinguishing from its neighbours. */
.videoOsdBottom .btnPause {
    background: rgba(255, 255, 255, 0.12);
    border-radius: 50%;
    margin: 0 0.3em;
}

.videoOsdBottom .btnPause:hover {
    background: rgba(255, 255, 255, 0.2);
}

/* Elapsed, remaining and "ends at" all change digit by digit; proportional figures make the
   surrounding layout twitch on every update. */
.osdPositionText,
.osdDurationText,
.osdTimeText,
.endsAtText {
    font-variant-numeric: tabular-nums;
}

.osdTitle {
    font-weight: 600;
    letter-spacing: -0.01em;
}

/* --------------------------------------------------------------------- motion ---------------- */

/* Jellyfin's own OSD fade. Giving it a decelerating curve rather than the default ease is most of
   what separates "appears" from "snaps on". */
.videoOsdBottom,
.skinHeader {
    transition: opacity 0.28s var(--jmp-ease), transform 0.28s var(--jmp-ease) !important;
}

.osdControls .paper-icon-button-light,
.videoOsdBottom .paper-icon-button-light {
    transition: transform 0.16s var(--jmp-ease), background-color 0.16s var(--jmp-ease-quick),
                opacity 0.16s var(--jmp-ease-quick);
}

.osdControls .paper-icon-button-light:hover,
.videoOsdBottom .paper-icon-button-light:hover {
    transform: scale(1.12);
}

.osdControls .paper-icon-button-light:active,
.videoOsdBottom .paper-icon-button-light:active {
    transform: scale(0.94);
    transition-duration: 0.07s;
}

@media (prefers-reduced-motion: reduce) {
    .mdl-slider-background-flex,
    .mdl-slider::-webkit-slider-thumb,
    .sliderBubble,
    .actionSheetMenuItem,
    .videoOsdBottom,
    .skinHeader,
    .osdControls .paper-icon-button-light,
    .videoOsdBottom .paper-icon-button-light {
        transition: none !important;
    }
}
`;

    function installStylesheet() {
        if (document.getElementById('jmp-player-skin')) {
            return;
        }

        const style = document.createElement('style');
        style.id = 'jmp-player-skin';
        style.textContent = SKIN_CSS;
        (document.head || document.documentElement).appendChild(style);
    }

    /**
     * Makes the played range follow the pointer while the transport slider is dragged.
     *
     * The OSD's slider carries `data-slider-keep-progress="true"`, and emby-slider reads that on
     * every `input` to decide whether to move the played range: with the flag set it leaves the
     * range showing real playback position for the whole drag and only catches up on `change`. So
     * the thumb tracks the pointer while the blue behind it sits still, and the bar appears to
     * jump on release. Clearing the flag restores the usual behaviour of the fill following the
     * thumb.
     *
     * This is safe because the OSD already stops writing playback position into the slider while
     * `slider.dragging` is set, so nothing competes for the value during the drag.
     *
     * The listener is on the document in the capture phase: capture reaches the document before
     * the element's own handlers, so the flag is always cleared before emby-slider reads it, and
     * binding to the document survives the OSD being rebuilt for every playback.
     */
    function keepFillUnderThumb() {
        document.addEventListener('input', (e) => {
            const slider = e.target;
            if (slider instanceof Element && slider.classList.contains('osdPositionSlider')) {
                delete slider.dataset.sliderKeepProgress;
            }
        }, true);
    }

    /**
     * Marks a slider as being dragged, which is what grows the track and the thumb for the
     * duration of the grab. Pointer capture means `pointerup` can land anywhere - off the slider,
     * off the window - so every container marked on the way down is cleared, not just whichever
     * one the pointer happens to be over.
     */
    function trackSliderDragging() {
        const dragging = new Set();

        document.addEventListener('pointerdown', (e) => {
            const container = e.target instanceof Element
                ? e.target.closest('.mdl-slider-container')
                : null;
            if (!container) {
                return;
            }
            dragging.add(container);
            container.classList.add('jmp-dragging');
        }, true);

        const endDrag = () => {
            for (const container of dragging) {
                container.classList.remove('jmp-dragging');
            }
            dragging.clear();
        };

        document.addEventListener('pointerup', endDrag, true);
        document.addEventListener('pointercancel', endDrag, true);
    }

    /**
     * Starts the time at the right of the transport slider on total duration rather than time
     * remaining.
     *
     * Jellyfin already toggles between the two when that text is clicked, and stores the choice;
     * only its default differs, so all this does is write the stored value once, on a profile that
     * has never been toggled. Anything the user picks afterwards is left alone - overwriting on
     * every launch would make the click look broken.
     *
     * jellyfin-web reads the value straight out of localStorage under "<userId>-<name>"
     * (userSettings.enableVideoRemainingTime -> appSettings.get -> localStorage), so the key is
     * written directly: the module that owns it is bundled and not reachable from here.
     *
     * The key is per user, and ApiClient only knows the user some way into jellyfin-web's own
     * bootstrap - well after this script runs, and with no event reachable from here to wait on -
     * so it is polled for. Polling stops on the first write, and gives up after ten minutes rather
     * than run for the life of a session that never signs in; missing the window only means the
     * default stays Jellyfin's, and one click still fixes it for good.
     */
    function defaultToTotalDuration() {
        const KEY = 'enableVideoRemainingTime';
        const INTERVAL_MS = 1000;
        const GIVE_UP_AFTER = 600;

        let attempts = 0;

        const apply = () => {
            const userId = window.ApiClient && window.ApiClient.getCurrentUserId
                ? window.ApiClient.getCurrentUserId()
                : null;

            if (!userId) {
                return ++attempts >= GIVE_UP_AFTER;
            }

            try {
                const storageKey = `${userId}-${KEY}`;
                if (localStorage.getItem(storageKey) === null) {
                    localStorage.setItem(storageKey, 'false');
                }
            } catch (e) {
                // Private mode or blocked site data: the default just stays Jellyfin's.
            }

            return true;
        };

        if (apply()) {
            return;
        }

        const timer = setInterval(() => {
            if (apply()) {
                clearInterval(timer);
            }
        }, INTERVAL_MS);
    }

    function install() {
        installStylesheet();
        keepFillUnderThumb();
        trackSliderDragging();
        defaultToTotalDuration();
    }

    // Injected at document creation, so <head> is usually not there yet.
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', install, { once: true });
    } else {
        install();
    }
})();
