# Decisions

## 2026-10-29 — Spec review

- **Module IDs `jwaveform` / `jtimeline`.** JUCE requires the folder name to equal the module ID, so hyphenated names are impossible; a `juce_` prefix risks clashing with official modules. Matches juce-grid's `jgrid`.
- **jtimeline depends on jwaveform at compile time.** `ClipInfo` carries a `WaveformSource`, so "optional at runtime" was not achievable without an extra abstraction layer that bought nothing.
- **`clipsInRange` and `length()` are required.** The fallbacks (filtering all clips, deriving the end from the last clip) are full scans and defeat the visible-cost goal.
- **`clipInfo` / `trackInfo` return `std::optional`.** The view needs to detect removed ids for selection and change handling.
- **No `Viewport`, no component per clip.** Content width at deep zoom on long timelines exceeds `int`, and float graphics coordinates lose precision past ~16M px. 20,000 components would not perform.
- **Accessibility via pooled proxy components for visible clips.** JUCE accessibility is component-based; there is no virtual-children API.
- **Every source matches the timeline rate.** Keeps one time unit throughout (offsets, lengths, peaks); conversion is the model's job. A per-clip source rate with display-only mapping was considered and dropped as unneeded complexity.
- **`int16` peaks, chunked non-reallocating storage, atomic valid length.** Memory (half of float) and lock-free reads of partial builds.
- **Raw-sample fetch for deep zoom.** Level 0 (64 samples/peak) is too coarse at pixel-per-sample zoom, and the message thread may not read audio.
- **No summed channel mode.** A sum cannot be derived from min/max pairs; combined mode is a min/max envelope.
- **Reader factory runs on background threads.** It is the single exception to the message-thread rule for model callbacks.

## 2026-10-29 — Spec review, second pass

- **Clips lie within `[0, length()]`, length at least 1.** Lets select-all use real bounds instead of int64 sentinels (which invite overflow in model code), and keeps zero-length clips from being unreachable.
- **Per-level valid counts in `PeakData`.** Deriving higher-level validity from level 0 is fragile; each level publishes its own.
- **Changed clips trigger a draw-order refresh of their tracks.** `ClipInfo` stays free of an order field; the view re-asks `clipsInRange` for the visible range instead.
- **Accessibility focus is decoupled from selection.** Screen-reader browsing must not destroy a multi-selection; selection goes through press/toggle actions.
- **Keyboard cursor can rest on a track.** Restores keyboard access to tracks and gives Up/Down a defined result when the adjacent track has no clip in view.
- **Zoom keys match by character, not key code.** Key codes are layout-dependent; characters are not.

## 2026-10-29 — Pre-implementation gaps

- **Application-owned, shared `PeakCache`.** Views of one model must not build the same source twice; `shared_ptr` keeps lifetime simple.
- **Accessibility proxies are visible, transparent and mouse-transparent.** JUCE omits invisible components from the accessibility tree.
- **Optional `adjacentClip`.** `clipsInRange` returns draw order, so finding the next clip by start off screen could otherwise mean a full-track query; doubling windows are the fallback.
- **Pooled track headers.** Headers are components; only visible tracks get one.
- **Level-of-detail tiers instead of culling.** Culling sub-pixel clips made zoomed-out tracks look empty; a coverage strip shows them. Optional `clipSpans` lets the model answer coverage from its index.
- **Explicit overview mode.** An overview shows the whole timeline, so it gets coverage-only drawing rather than relying on automatic tiers.
- **Gain in dB.** Matches how users think about and edit gain; the view converts for drawing.
- **Disk cache written temp-then-move.** No partial files after a crash or under concurrent access.

## 2026-10-29 — Waveform zoom

- **Waveform zoom is a view-level display scale with per-track overrides.** It is separate from clip gain (model data) and never rebuilds peaks; overrides cover the common case of one quiet track.

## 2026-10-29 — Pro Tools input bindings

- **Mouse and keyboard bindings match Pro Tools defaults.** Target users know them. The `+`/`-` zoom keys and the earlier waveform-zoom modifiers are dropped; per-track waveform zoom is API only.
- **Windows/Linux track height is Ctrl + Up/Down, not Start + Up/Down.** Pro Tools' Start-key mapping collides with Windows window snapping; Ctrl is free because Command + Up/Down is unused.
- **Zoom anchor "edit cursor" is the time-selection start, else the playhead.** The view has no separate insertion point.

## 2026-10-29 — Selection model, group clips, DPI

- **Object selection stays; only scroll and zoom follow Pro Tools.** Pro Tools' range-based selection was considered and declined.
- **Group clip kind reserved, contents not modelled.** Leaves room for sessions with clip groups (including empty groups used as notes) without the view knowing about nesting.
- **Peak density by physical pixels.** JUCE renders at physical resolution but the data density is ours to choose.

## 2026-10-29 — No gain lines

- **Gain lines and gain editing removed.** Out of scope. Clip gain remains model data that scales the drawn waveform.

## 2026-10-29 — Scope tightening

- **`adjacentClip` and `clipSpans` required.** Fallbacks were second, slower implementations to write and test.
- **`PeakBuilder` takes `AudioFormatReader` only.** Other sources can be wrapped as readers.
- **No per-clip painter lambda.** The look and feel already covers custom drawing.
- **No logarithmic amplitude.** Waveform zoom covers quiet material.
- **On-disk peak cache deferred to the last milestone; deep zoom deferred to after selection and accessibility.** Both are separable subsystems nothing else depends on.
- **Page follow only.** Smooth-scroll follow was the biggest threat to frame rate.
- **No built-in alternative ruler formatters or tempo map.** The formatter lambda covers them.

## 2026-10-29 — Milestone 1

- **View start is an integer sample plus a fraction.** With an integer start, zoom anchoring drifts by up to a sample width (16 px at the deepest zoom). A single `double` would lose sub-sample precision on long timelines.
- **Peak level choice: the coarsest level with at least one peak per physical pixel.** "Nearest" could pick a level coarser than a pixel and lose detail.
- **`SamplePosition` and `SampleRange` live in jwaveform.** jtimeline depends on jwaveform anyway, and one definition avoids conversions between the modules.
- **Build mirrors juce-grid.** Same presets, sanitizer and clang-tidy options, and `tools/check.py`.
- **Wheel bindings in `core/`, one notch per notched-wheel event.** Per-notch delta sizes differ about sixfold between platforms, so counting events gives the same zoom per notch everywhere. Trackpad deltas are proportional instead. The milestone 1 demo had ignored the spec's bindings (plain wheel scrolled time); it now uses the shared logic.

## 2026-10-29 — Milestone 2

- **`PeakCache` is used from the message thread.** The spec said thread-safe; all GUI code is on the message thread anyway, and confining it there keeps listener callbacks and timers simple. Builds still run on the pool and only touch their own entry.
- **Handles pin entries.** Peaks held by a handle are not evicted, so the cache's memory is actually freed when it evicts; the bound may be exceeded while everything is in use.
- **Cancelled builds resume.** `PeakData` keeps its partial block, so a new reader continues from the appended frame count instead of starting again.
- **Failed entries are dropped once unused.** A later request retries, which covers a file that reappears.
- **Notifications by polling timer.** Builders set a flag; a 30 ms timer on the message thread delivers callbacks, which rate-limits per key and avoids posting a message per block.
- **Outward rounding of peaks.** Rounding min down and max up never understates the envelope after `int16` quantisation.
- **`append` takes a span of channel pointers.** Callers pass arrays or vectors without raw pointer-to-pointer decay.
- **Separate waveform demo.** jwaveform is usable without the timeline, so it gets its own app; the timeline demo moved to `demo/timeline/`.

## 2026-10-29 — Milestone 2 QA feedback

- **Option/Alt + A zooms to fit.** Requested; Pro Tools users expect a one-key "show everything".
- **Keys matched by key code, not typed character.** Option changes the typed character on macOS (Option + A types å), which made the typed-character rule unusable for Option bindings. JUCE's key code is the layout's character without modifiers, so layout independence is kept.
- **Option/Alt + Shift + wheel up makes waveforms smaller.** Requested direction.
- **No light/dark appearance following.** The modules are embedded in host applications, which decide the colour scheme.

## 2026-10-29 — Milestone 3

- **Ids are `enum class` types.** Distinct, hashable and free to compare, with no wrapper code; a `TrackId` cannot be passed as a `ClipId`.
- **`clipSpans` is optional.** Small models can skip it; the view falls back to `clipsInRange` and `clipInfo`. `mergeSpans` makes it a few lines for models that have it.
- **All track infos are read on track changes.** The layout needs every track's height hint; tracks number in the hundreds at most, unlike clips.
- **Dense tracks skip per-clip queries.** Above one clip per 2 px, nearly every clip would be coverage or minimal anyway, and querying 20,000 `ClipInfo`s per zoom step is what the spans callback exists to avoid.
- **Clip ids are cached for the visible range plus half a view either side.** Small scrolls and repaints make no model calls; zoom re-queries.
- **Headers are `TrackHeader` subclasses, with `trackChanged()` as the rebind callback.** One factory returning a component that knows how to rebind is simpler than a factory plus a separate callback.
- **Look and feel methods have default bodies.** A host look and feel derives from `TimelineLookAndFeelMethods` and overrides only what it needs, instead of implementing every method as JUCE's pure-virtual convention would require.
- **Horizontal overscroll of 10% each side.** Allows pre-roll before 0 and a little room past the end without letting the timeline scroll away.
- **Full zoom range while clips have no waveforms.** The level-0 limit exists for waveform sharpness; it applies from milestone 4.
- **Ruler clicks report positions; the view does not move the playhead itself.** The playhead belongs to the application's transport.
- **Home/End in `keys::actionFor`.** All key bindings stay in one tested place.

## 2026-10-29 — Milestone 4

- **One peak handle per source key per view, kept while painted.** Handles are requested while painting and released after a paint of the whole clip area that did not use them. Zooming out to coverage or scrolling away cancels unfinished builds without separate visibility bookkeeping; partial paints never release anything.
- **Ready-made peaks draw without a cache.** They need no building; only cache-built sources depend on `setPeakCache`.
- **Rate mismatch is checked before the peak state.** A mismatched source never draws, even partially, since its positions would be wrong.
- **Overlapping fades multiply.** Fades are clamped to the clip length rather than to each other; the envelope stays well defined without inventing a rule.
- **Zoom limit uses the timeline's display.** The physical scale is the display's scale times the component's scale factor, so it is right on mixed-DPI setups after the next zoom.
- **Shared demo source.** Both demos use one synthetic reader with variants, so the timeline demo has distinct waveforms without audio files.

## 2026-10-29 — Milestone 5

- **Markers and the timeline selection are optional model callbacks.** `markersInRange` keeps marker reads proportional to the view, like clips; the timeline selection is a single value. Both share the existing `MarkersChanged` notification.
- **Markers get their own strip above the time scale.** Tick labels already fill the time scale; the strip appears only for models that have markers or a timeline selection, so other applications keep the 28 px ruler.
- **A ruler drag selects time; a click still reports a position.** Scrubbing by drag was dropped in favour of the spec's time selection. Clicking a marker reports its position, so locating to markers needs no extra callback.
- **Shift adds, Command/Ctrl toggles.** The spec named both modifiers without assigning them; this follows common file-browser behaviour. (Superseded after QA: Shift + click selects a range.)
- **Replace-mode clicks clear the other object kind.** Clicking a clip deselects tracks and vice versa, so a plain click always leaves a single selected object; Select all leaves tracks alone.
- **Keys that do not move the cursor leave the selection alone.** Pressing Right on the last clip should not collapse a multi-selection.
- **Unconsumed Escape.** Escape on a track does nothing and is passed on, so hosts can use it to close panels.
- **Proxies only for reduced and full clips, at most 1000.** JUCE cannot report whether a screen reader is running, so proxies are always maintained; narrower clips are not individually useful to a screen reader, and the cap bounds the per-scroll cost (about 2 ms at the cap).
- **Accessibility handlers need a window.** JUCE creates them only for components on a peer, so the accessibility test puts the timeline on the desktop.
- **Demo answers `adjacentClip`.** It shows the intended model shape; the doubling-window fallback is covered by unit tests.
- **"Timeline selection", not "loop range".** Requested. It is the model's range (as in Pro Tools' timeline selection), whatever the application uses it for; the view's ruler-drag range stays the "time selection".

## 2026-10-30 — Milestone 5 QA feedback

- **No group badge; square audio clips, rounded groups.** Requested. The corner shape alone tells groups apart, with the heavier outline.
- **Clip names overlay the waveform.** Requested, so the waveform is centred in the clip; a 1 px shadow keeps the name legible.
- **Option/Alt + F zooms to the selection.** Requested; Pro Tools users know it.
- **Select all is scoped to selected tracks.** Requested: with tracks selected, Command/Ctrl + A selects their clips only.
- **Option/Alt + wheel zooms around the selected clips.** Requested. Only that gesture changes; keys keep the edit cursor, since the user asked only for the wheel. The span is cached because select-all can hold thousands of clips and wheel events are frequent.
- **Waveform columns snapped to physical pixels.** Columns were placed relative to the clip's fractional left edge and straddled physical pixels, so anti-aliasing striped dense waveforms and dotted quiet ones.
- **Track and clip cursors are the keyboard cursor's parts.** Shift + click needs an anchor; the keyboard cursor already has a track and a clip, so they serve, and Shift + click leaves them in place so repeated Shift-clicks re-range from the same anchor.
- **Shift + click selects a rectangle, not one more clip.** Requested: from the cursor clip to the clicked clip across tracks and time, by overlap like the rubber band. Command/Ctrl remains the way to add single clips.
- **Shift + drag anywhere is a rubber band.** Requested, so clips can be band-selected without empty space to start from; a plain drag on a clip stays free for moving clips in milestone 8.
- **Selected tracks follow selection gestures on clips.** Requested, so track height keys act on the tracks of selected clips. API setters stay literal so applications can set any combination.
- **One zoom anchor for wheel and keys.** Requested: T/R and Command/Ctrl + ]/[ also zoom around the selected clips. `zoomAtEditCursor` became `zoomAtAnchor`.
- **Space reports, the application plays.** The view never plays audio, so Space only calls `onPlayStop`.

