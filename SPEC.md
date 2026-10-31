# JUCE Timeline — Specification (draft)

A pair of JUCE modules for viewing, and later editing, an audio timeline: tracks containing clips, with waveform previews. Private repository. For now it is local only: there is no GitHub repository yet, so no remote, PRs or hosted CI until one is created. Targets JUCE 8.

## Modules

- **jwaveform**: waveform peaks and drawing. Depends on `juce_audio_formats` and `juce_gui_basics`. Usable on its own, with no timeline.
- **jtimeline**: the timeline view. Depends on `jwaveform` and `juce_gui_basics` (a hard dependency: `ClipInfo` carries jwaveform types). A clip with no waveform source draws as a plain block.
- Module folders are `modules/jwaveform` and `modules/jtimeline`, each with an umbrella header and source named after the folder (`jwaveform.h`, `jwaveform.cpp`), as JUCE requires.
- Both: C++23 minimum, same conventions as juce-grid (`core/` for non-GUI logic, a component folder, one umbrella header per module).
- Shared value types live in jwaveform and are aliased into `jtimeline`: `SamplePosition` (`int64`) and `SampleRange` (half-open `[start, end)`).

## Platforms and CI

- Windows, macOS, Linux. Platform differences go through JUCE.
- Develop on macOS (arm64). Build with CMake and Ninja Multi-Config.
- Once a GitHub repository exists (not yet), CI is a single manually triggered workflow (`workflow_dispatch` only, no push, PR or schedule triggers) building and testing on Linux (GCC, under xvfb) and Windows (MSVC). Run it once to check portability, and again before a release. Sanitizers and clang-tidy run locally.
- Tests: Catch2, with the model-independent logic (time mapping, hit testing, peak generation, snapping) tested without a display.

## Concepts

- **Timeline**: an ordered list of tracks over a shared time axis.
- **Track**: a lane holding clips. Overlapping clips on one track are allowed and drawn overlapping, in draw order (later on top), with the overlap visible: clip bodies are slightly translucent and each clip keeps its own outline, so the clip underneath can be seen. Hit testing picks the topmost clip.
- **Draw order**: a per-track total order of clips defined by the model. It is the same whichever range is queried.
- **Clip**: a span of time on a track, referring to a region of a source. Has a track, a start, a length of at least 1 sample, a source offset, a name and a colour. Clips lie within `[0, length()]` of the timeline. Optional display-only gain (in dB) and fades.
- **Clip kind**: `audio` (the default) or `group`. A group clip stands for a group of clips the model keeps elsewhere; the view does not model or draw its contents. It has a span, name and colour like any clip but no source, so it never has a waveform or fades; its body uses a look-and-feel group style (rounded corners and a distinct outline). Empty groups are drawn the same way. Selection, hit testing, level of detail, accessibility and edit requests treat it as a clip; what an edit means for its contents is the model's business.
- **Time unit**: signed 64-bit sample positions at a timeline sample rate supplied by the model. Timeline positions and clip lengths are in timeline samples. Sub-sample positions are not represented. The view may scroll before 0 (pre-roll) but no clip starts there. The default display is hours:minutes:seconds.milliseconds; SMPTE timecode and bars/beats come later (roadmap).
- **Source rate**: every source is at the timeline sample rate, so a clip's source offset and source length are in timeline samples, and a timeline position `p` inside a clip is source sample `sourceOffset + (p - start)`. Converting sources is the model's job. A waveform source whose sample rate differs is shown as a failure state (rate mismatch), not drawn.
- **Identity**: tracks and clips are identified by stable opaque ids (`TrackId`, `ClipId`: `enum class` over `uint64_t`, so they are distinct, comparable and hashable types), not indices, because clips move between tracks and reorder freely. Track order is the model's. Unlike juce-grid, nothing in the API is index-based except track order.

## Model communication

Same approach as juce-grid: a struct of `std::function` members, no inheritance, the view is not a template. Bound with `timeline.setModel(TimelineModel)`.

View pulls from the model (all required unless marked optional):
- `trackIds()` → ordered list of `TrackId`
- `trackInfo(TrackId)` → `std::optional<TrackInfo>`: name, colour, height hint (optional fields). Empty means the track does not exist.
- `clipsInRange(TrackId, rangeStart, rangeEnd)` → ids of the clips on the track with `clip.start < rangeEnd && clip.start + clip.length > rangeStart`, in draw order. So a clip starting before the range but extending into it is included, and one ending exactly at `rangeStart` is not. The view always passes `0 <= rangeStart < rangeEnd <= length()`, never sentinel extremes. The model is expected to answer this without scanning the whole track (e.g. a sorted index or interval tree); the view never asks for all clips except for select-all.
- `clipInfo(ClipId)` → `std::optional<ClipInfo>`: kind, track, start, length, source offset, source length (optional), name, colour, gain (dB, 0 = unity), fade in, fade out, muted, optional `WaveformSource`. Empty means the clip does not exist. A reported length of 0 is a model error; the view treats it as 1.
- `sampleRate()`
- `length()`: the timeline end in samples. The model tracks it; the view never derives it.
- `adjacentClip(TrackId, ClipId, direction)` → the previous or next clip on the track in start order (ties by draw order), or empty at either end. Used by Left/Right.
- `clipSpans(TrackId, rangeStart, rangeEnd, minGap)` (optional) → the occupied spans of the track within the range, merged where clips overlap or the gap between them is smaller than `minGap` samples, sorted by start. Lets coarse drawing (see Level of detail) avoid querying every clip; without it the view computes spans from `clipsInRange` and `clipInfo`. `mergeSpans(spans, minGap)` (sorts, then merges overlapping, touching and closer-than-`minGap` spans) helps implement it.
- `notifier`: a `TimelineNotifier*`, or null (then the application calls `refresh()` after changes).
- `markersInRange(rangeStart, rangeEnd)` (optional) → the `Marker`s (`MarkerId`, position, name, optional colour) with `rangeStart <= position < rangeEnd`, sorted by position. The view passes the visible range (widened by a label width on the left) plus half a view either side and caches the result until the range leaves it or markers change; ranges may extend outside `[0, length()]`.
- `timelineSelection()` (optional) → the timeline selection, or empty: a range the model owns (for example the range the application plays or loops), distinct from the view's time selection. Read on binding, markers changed and full reset.
- `adjacentClip`, `clipSpans`, `markersInRange` and `timelineSelection` are optional. A model missing any other required callback shows as empty. Without `adjacentClip`, keyboard navigation queries `clipsInRange` over windows starting at the visible span (at least 1024 samples) and doubling until one holds an answer or reaches the timeline end.

Reads happen only for visible clips, plus selected ids when checking they still exist. `trackIds` and `trackInfo` for every track are read on binding, on tracks changed and on full reset (the layout needs every track's height hint). The view caches, per visible track, the clip ids for the visible range extended by half a view width on each side, and the `ClipInfo` of those clips; scrolling within that range and repainting make no model calls. Changing zoom re-queries.

Model pushes to the view: a `TimelineNotifier` owned by the model, one `changed(Change)` call, always on the message thread. `Change` is a variant: tracks changed (added, removed, reordered), clips changed (a set of ids, covering add, remove, move and property edits), markers changed, or full reset. A change of `sampleRate()` or `length()` is notified as a full reset. No dirty flag.

On clips changed, the view re-queries each id (in overview mode it skips this and recomputes coverage instead). It repaints the clip's old rectangle (from its per-layout geometry cache, so a clip that was off screen needs no repaint) and its new one. An id whose `clipInfo` is empty is treated as removed. Because `ClipInfo` carries no draw-order position, every track touched by a changed id (its old track from the cache and its new one from `clipInfo`) has `clipsInRange` called again for the visible range, refreshing the cached draw order used for painting and hit testing.

Lifetime: as juce-grid. `TimelineNotifier` has a `Listener` interface (`modelChanged`, `notifierDestroyed`), which `Timeline` implements privately. The view never owns the model; `setModel` subscribes to `model.notifier` and unsubscribes from the previous one; a destroyed notifier detaches its views, which then show as empty.

Multiple views of one model are supported (e.g. an overview strip and a main view). Each view has its own zoom, scroll, selection and track heights.

## Waveforms (jwaveform)

Data:
- `PeakData`: a multi-resolution min/max pyramid for one audio source, with the source's sample rate, length and channel count. Level 0 is one min/max pair per N samples (default 64) per channel; each higher level halves resolution. Values are `int16` normalised to ±1 (32767 = 1), rounded outwards (min down, max up) so the envelope is never understated; samples beyond ±1 are clamped and NaN samples ignored (a block with no other values stores 0). An hour of 48 kHz stereo is about 43 MB including the pyramid (level 0 about 22 MB).
- Writing: one writer calls `append(span of channel pointers, frames)` and finally `complete()`, which writes the partial last block and the remaining higher-level pairs. `complete()` is also used when a source turns out shorter than its stated length. Frames past the stated length are ignored. The writer can resume later from `getFramesAppended()`.
- Storage is chunked in fixed-size blocks that never reallocate once written. Each level has its own atomically published count of valid pairs (release on write, acquire on read), so a reader can draw a partially built `PeakData` while the builder appends. Higher levels are filled as the pairs below them complete; a reader reads each level only up to that level's count, and draws the remainder of the range from the coarsest level that covers it (at worst level 0).
- `PeakData` is shared as `std::shared_ptr<const PeakData>` (the builder holds the only mutable access). Eviction drops the cache's reference; anything drawing keeps its own until done.

Building:
- `PeakBuilder`: builds `PeakData` from an `AudioFormatReader` on a background thread, progressively (blocks of 65536 frames, published after each), so partial results can be drawn while analysis continues. It resumes from the data's appended frame count, checks a cancel flag before each block, and reports `complete`, `cancelled` or `readError`; after a read error the data keeps what was read and is not complete.
- Builds run on a thread pool owned by the cache (default 2 threads, configurable). A build is cancelled when no handle requests its key any more; finished or partial results stay cached, and a cancelled build resumes where it stopped (with a new reader) when the key is requested again.

Cache:
- `PeakCache`: shared, keyed by a source key supplied by the model (e.g. file path plus modification time). One build per key however many clips use it. Its methods are called on the message thread (as all GUI code is); only builds run elsewhere.
- `request(WaveformSource, onChange)` returns a move-only `PeakHandle`. While any handle for a key exists, the build continues and the peaks are not evicted. The handle gives the possibly partial `PeakData` (empty until a build has opened the source) and a state: `none`, `loading`, `ready`, `cannotOpen` (the factory returned null or threw) or `readError`. `onChange` is called on the message thread when peaks grow or the state changes, at most once per key per 30 ms (configurable), and never after its handle is destroyed, even when another callback destroys it. Handles may outlive the cache. A source with ready-made peaks gives a handle holding them directly (`PeakHandle::forPeaks`), with no cache entry.
- Memory-bounded in bytes (default 256 MB, configurable) with LRU eviction. Only entries no handle holds and no build is writing are evicted, least recently requested first; while every entry is in use the bound may be exceeded. Failed entries are dropped as soon as no handle holds them, so a later request retries.
- Optional on-disk cache (milestone 10): off unless the application sets a directory (`setDiskCacheDirectory`); jwaveform never picks a location. One file per source, named by a hash of the key. Files are written to a temporary file in the same directory and then moved over the target, so a crash or concurrent reader never sees a partial file. The header holds a format version, the full key, sample rate, length and channel count; any mismatch means the file is ignored and rebuilt.

Sources:
- The cache is owned by the application and handed to each timeline as `timeline.setPeakCache(std::shared_ptr<PeakCache>)`, so several views (e.g. an overview and a main view) share one cache and build each source once. Without a cache, only sources with ready-made peaks draw. `PeakCache::getSamplesPerPeak()` reports the level-0 resolution it builds.
- `WaveformSource` (what a clip hands to the view): a key plus a reader factory, or a ready `PeakData`. The model owns file management; jwaveform never decides where audio lives.
- The reader factory is called on background threads, never on the message thread, and must be thread-safe. It is the one model-supplied callback exempt from the message-thread rule. Each call returns a new reader, so readers are never shared between threads.

Deep zoom (milestone 6):
- When the view shows fewer source samples per pixel than level 0 holds, the drawing needs raw samples. A `SampleCache` (owned by `PeakCache`, same memory bound) fetches the visible span of raw samples through the reader factory on the pool, in fixed-size blocks with LRU eviction.
- Until the raw samples arrive, level 0 is drawn stretched. A source that is only a ready `PeakData` (no reader) always draws level 0 stretched.
- With raw samples, the waveform draws as a line through the samples, with a dot per sample once samples are several pixels apart.

Drawing:
- `drawWaveform(Graphics, area, PeakData, sourceStart, sourceEnd, WaveformStyle)` paints a source span (fractional sample positions) across a rectangle, computing only the part inside the graphics clip region, one column per physical pixel, each filled from its min to its max (at least one physical pixel tall). Columns and their top and bottom edges are snapped to the physical pixel grid of the graphics context, and only whole columns inside the area are drawn, so nothing is anti-aliased whatever the area's fractional position. Columns with no data yet are left empty. `WaveformStyle` holds channel mode, minimum lane height, display scale, envelope (gain in dB and linear fade-in/fade-out source ranges, evaluated at each column's centre) and colours. The column computation (`computeColumns`) is in `core/` and tested without graphics.
- `WaveformView`: a component showing a source span (empty span = whole source) through a `PeakCache`, repainting on handle callbacks, with text for loading-without-peaks and failure states. Colour ids: background (default transparent), waveform (default `Slider::thumbColourId`), centre line (default label text at 25% alpha) and text (default `Label::textColourId`), resolved through the component, its parents, then the look and feel. Drawing picks the coarsest level that still has at least one peak per physical pixel (logical width × `Graphics::getInternalContext().getPhysicalPixelScaleFactor()`), so waveforms stay sharp on high-DPI displays; cost is O(physical pixels).
- Channel modes: stacked (one lane per channel), or combined (one lane showing the min of the mins and the max of the maxes across channels). There is no summed mode, as a sum can't be derived from min/max pairs. Stacked falls back to combined when a lane would be shorter than a look-and-feel minimum height (default 12 px).
- A vertical display scale (linear factor, default 1), separate from gain: it multiplies amplitudes. Amplitudes beyond the lane are clamped to its edge.
- Optional gain scaling (gain in dB, converted to linear for drawing) and gain envelope (fade curves multiplied in, so the waveform shrinks through a fade), colours from the look and feel. Amplitude is linear.
- Repaints when the cache reports progress, rate-limited to one repaint per view per 30 ms. A repaint covers only the union of the cached rectangles of visible clips using the affected key, never the whole clip area.
- In a timeline, each view holds one `PeakHandle` per source key. It requests one while painting a clip that draws a waveform (an audio clip with a source, at the full or reduced tier, whose body is at least the minimum waveform height tall), and releases handles that a paint of the whole clip area did not use, so zooming out or scrolling away cancels unfinished builds. Overview mode never requests peaks.
- A clip's waveform status (`core/`, `waveformStatusFor`): none, loading, ready, cannot open, read error, or rate mismatch (peaks whose sample rate differs from the timeline's; checked before the peak state). Loading, ready and read error draw whatever peaks exist. At the full tier the look and feel labels failures "Offline", "Read error" and "Sample rate mismatch".
- The waveform's source span follows the clip: the source position at pixel `x` is `sourceOffset + (x - clipStartX) × samplesPerPixel`, computed for the clip's cut bounds. `envelopeFor(ClipInfo)` gives the drawn envelope: the clip gain, a fade-in over the first `fadeIn` source samples of the clip and a fade-out over the last `fadeOut`, each clamped to the clip length (overlapping fades multiply).
- Failure (missing file, unreadable) is reported as a state the clip can show, not an exception.

## View behaviour (read-only, v1)

Rendering:
- The view is not built on `juce::Viewport`. Scrolling is virtual: the view state is a `double` samples-per-pixel and a start position made of an `int64` sample plus a `double` fraction in [0, 1), driven by `juce::ScrollBar`s. The fraction keeps zoom anchoring exact at deep zoom, where one sample spans many pixels. A content-sized component would exceed `int` range at deep zoom on long timelines.
- Pixel positions are computed relative to the view start in `double`, and clamped to the visible area plus a small margin (8 px for clips) before conversion to `float`, so graphics coordinates stay small. `ClipPaint` tells the look and feel whether a clip was cut at either edge.
- Horizontal scroll: the left edge may go from 10% of the view span before 0 to 10% of the view span past the point where the timeline end (at least 10 s) meets the right edge. So zoom to fit leaves a little play either way.
- The `Timeline` is split into child components: a `Ruler` on top (right of the header column), a header column, the clip area, and a horizontal and a vertical `juce::ScrollBar` (always shown). Wheel and pinch events from any child reach the `Timeline`, which handles them; the `Timeline` takes keyboard focus when its ruler or clip area is clicked.
- Clips are painted by the clip area, not represented by one component each. Per-clip components exist only as accessibility proxies (see Accessibility).

Layout:
- Track headers on the left (name, colour; contents customisable by a component factory lambda for mute/solo/arm buttons). Headers are `TrackHeader` components (or subclasses): `setTrackHeaderFactory(std::function<std::unique_ptr<TrackHeader>()>)`. Header components exist only for visible tracks, kept in a pool and rebound to a different `TrackId` as tracks scroll in and out; headers still on screen keep their track. The factory is called only when the pool needs another component; the virtual `trackChanged()` is the rebind callback, called when the bound track or its info changes, so custom contents can update. The default header is drawn by the look and feel. Time ruler on top. Clip area fills the rest.
- Track heights: `getTrackHeight(id)` is the height set in this view (`setTrackHeight`, `stepTrackHeights`), else the model's height hint, else the default (`setDefaultTrackHeight`, which also clears set heights; else the look-and-feel default, 72 px). Heights are kept by id across track changes.
- Track heights per track, with a global default. Keyboard height changes step through a look-and-feel list of heights (default 24, 32, 48, 72, 112, 176, 272 px); a track between steps moves to the next step in that direction. Vertical scroll for tracks; horizontal scroll for time.
- Playhead drawn from `setPlayhead(samples)`; the view never plays audio. Space calls `onPlayStop` (not consumed if it is unset). Only the old and new playhead strips are repainted. Optional page follow mode (`setFollowPlayhead`): when the playhead leaves the visible range, the view jumps so the playhead is at its left edge (as far as the scroll range allows). There is no smooth-scroll follow.
- A click on the ruler reports the nearest sample position, clamped to `[0, length()]`, through `onTimeClicked`, and clears the time selection; the application decides what to do with the position (the demo moves the playhead). Dragging at least 3 px from there selects the time range between the press and the pointer instead (no further `onTimeClicked`). `onViewChanged` reports zoom and scroll changes.
- Markers and the timeline selection are drawn in a strip above the ruler's time scale (`markerStripHeight`), present only when the model has `markersInRange` or `timelineSelection`: the timeline selection as a bar drawn over the markers, each marker as a flag with its name to the right, cut short before the next marker. A click within a few pixels of a marker's flag reports the marker's position through `onTimeClicked`.

Zoom and scroll:
- Zoom is samples-per-pixel (`double`; below 1 at deep zoom).
- **Edit cursor**: the start of the time selection if there is one, otherwise the playhead. - **Zoom anchor** (`getZoomAnchor`): the centre of the selected clips' span while clips are selected, otherwise the edit cursor. It anchors every zoom not anchored at the pointer (wheel and keys): its screen position stays fixed, or if it is off screen, the view centres on it.
- Bindings follow Pro Tools defaults (see Input bindings). Trackpad horizontal deltas scroll horizontally directly; pinch (`mouseMagnify`) zooms around the pointer.
- API: `setMapping` (clamped to limits and scroll range), `setVisibleRange`, `zoomToFit`, `zoomAtAnchor(steps, factor)`, `scrollTo`, `setTrackScroll`, and `zoomToSelection` (Option/Alt + F). Home and End scroll to the timeline start and end.
- Limits: from 1/16 samples per pixel (16 px per sample) up to the whole timeline fitting in the view. While clips have no waveforms (milestone 3), the full range is available; from milestone 4 until deep zoom lands in milestone 6, the deepest zoom is the level-0 block size per physical pixel: the cache's samples per peak (64 without a cache) divided by the display scale of the timeline's display times the component's scale factor. For zoom-out purposes the timeline is at least 10 seconds long, so an empty timeline still has a range.

Ruler:
- Ticks (`ruler::scaleFor`, `ruler::ticksFor` in `core/`): major ticks at the smallest round spacing at least 90 px apart: 1, 2 or 5 × 10^n seconds from 1 µs to 0.5 s; 1, 2, 5, 10, 15 and 30 seconds and minutes; 1, 2 and 5 × 10^n hours. Minor ticks subdivide a major spacing (by 10, 5, 4, 3, 2 or 6 as suits the spacing, the finest at least 6 px apart). At sample-level zoom (a sample at least 6 px wide), minor ticks mark individual samples. Tick positions are fractional samples.
- Default formatter (`ruler::formatTime`): `m:ss`, or `h:mm:ss` from an hour, plus milliseconds when the major spacing is not a whole number of seconds, and more digits (down to microseconds) when it is under a millisecond. Negative positions are prefixed with `-`.
- A custom formatter, `setRulerFormatter(std::function<String(double position, double sampleRate, double majorSpacingSamples)>)`, replaces the labels (applications supply samples, timecode or bars and beats this way); tick positions stay time-based.

Clips:
- Drawn as blocks with name, colour, waveform and fades as supplied: audio clips with square corners, group clips rounded. The waveform fills the body (less 1 px top and bottom), centred vertically; at the full tier the name is drawn over its top-left with a 1 px shadow. Fades shade the area above a straight line from the clip's bottom corner to the top at the fade's end (fade-in) or from the top at the fade's start to the bottom corner (fade-out). The colour is the clip's, else its track's, else `clipColourId`. Group clips get rounded corners and a thicker, lighter outline. Clip gain scales the waveform but is not drawn as a line. Fades are linear in v1, drawn as an overlay shape and applied to the waveform envelope. Muted clips are dimmed.
- Custom clip drawing goes through the look and feel; there is no per-clip painter lambda.
- Painting is clipped to the visible range.

Waveform zoom:
- Each view has a waveform vertical scale, `setWaveformScale(factor)`, from 0.25× to 64× (default 1×), passed to the waveform drawing as its display scale. It affects drawing only: no peak data is rebuilt and the model is not told.
- A track may override it with `setTrackWaveformScale(TrackId, std::optional<double>)`; empty means the view's scale.
- Mouse and keyboard changes (see Input bindings) scale the whole view in steps of 2^(1/4); per-track scale is API only. Each scale change notifies a callback, so an application can persist it.

Level of detail:
- Each clip is drawn at a tier chosen by its on-screen width (thresholds from the look and feel):
  - **Full** (default ≥ 40 px): rounded, translucent body, outline, name, waveform, fades.
  - **Reduced** (≥ 4 px): rounded body and outline, waveform; no name or fades.
  - **Minimal** (≥ 1 px): opaque filled rectangle in the clip colour, no outline or waveform.
  - **Coverage** (< 1 px): not drawn individually. The track's occupied spans (from `clipSpans` with `minGap` of one pixel's worth of samples) are filled as a single strip in the track colour, so densely packed short clips stay visible when zoomed out.
- Waveforms are also skipped when the lane is shorter than the look-and-feel minimum height.
- Coverage strips for a track's sub-pixel clips are their timeline ranges merged with `minGap` of one pixel's worth of samples, drawn behind the track's other clips.
- **Dense tracks**: when a track has more than one clip per 2 px in its queried range, the view queries no `ClipInfo` for it and draws the whole track as coverage from `clipSpans`. At that density nearly every clip is under a few pixels wide.
- Hit testing and selection work on individual clips at every tier. A click in a coverage strip hits the topmost clip under the pointer, found by `clipsInRange` on a one-pixel range.
- **Overview mode** (`setOverviewMode(true)`), for an overview strip: the whole timeline (at least 10 s) across the full width, with every track squeezed to an equal share of the height, drawn in coverage form only (`clipSpans` over the whole timeline, cached until the width or model changes), with no per-clip queries, waveforms, headers, ruler, scroll bars, zoom or clip selection. The view draws the playhead and a visible-range rectangle set by `setHighlightedRange`, and reports clicks and drags as time positions through `onTimeClicked`, so the application can move another view.

Selection:
- Selection is object-based (clips and tracks), not Pro Tools' range-on-tracks model; only scrolling and zooming follow Pro Tools.
- A `Selection` (`core/`) holds clip ids, track ids and an optional non-empty time range. Selecting in `replace` mode selects only the item and deselects every object of the other kind; `add` adds it; `toggle` adds or removes it. The time range is independent of clips and tracks.
- Clicks: a click on a clip selects it (Command/Ctrl toggles, otherwise replace) and moves the keyboard cursor to it; a click on a track header does the same for the track. Shift + click selects a range from the keyboard cursor, which stays where it is as the range's anchor: on a clip, every clip overlapping the rectangle from the cursor clip to the clicked clip (the tracks between them inclusive, from the earlier start to the later end), replacing the clip selection; on a header, the tracks from the track cursor (the cursor's track) to the clicked track, deselecting clips. Without a cursor clip or track, Shift + click acts as a plain click.
- Rubber band: a press on empty clip area without modifiers deselects clips and tracks; dragging at least 3 px from empty space, or from anywhere with Shift held (including on a clip), draws a rubber band, and the clips in the tracks it crosses over its time span (`clipsInRange`; a zero-height band counts as one pixel high) become the selection, added to the clips selected at the press when Shift or Command/Ctrl is held. A Shift press on a clip that is released without dragging is a Shift + click.
- Tracks follow clips: after every selection gesture on clips (clicks, Shift + click, rubber band, keyboard moves onto clips, select all, accessibility actions), the selected tracks become the tracks holding selected clips, so track height keys act on them. API setters (`setSelectedClips`, `setSelectedTracks`) set exactly what they are given.
- The time range comes from dragging on the ruler (see Layout).
- API: `getSelection`, `getSelectedClips` (sorted by id), `getSelectedTracks` (display order), `isClipSelected`, `isTrackSelected`, `getTimeSelection`, `setSelectedClips`, `setSelectedTracks`, `setTimeSelection` (clamped to `[0, length()]`), `selectClip`/`selectTrack(id, mode)` (as a click, including the cursor move), `extendClipSelection`/`extendTrackSelection(id)` (as Shift + click), `selectAllClips`, `clearSelection` (everything, including the time range), `zoomToSelection` (the time selection, else the span of the selected clips) and `getSelectedClipSpan` (from the earliest start to the latest end of the selected clips; cached until the selection, clips or model change). `onSelectionChanged` is called after every change, whatever the cause, including API calls and ids dropped after model changes; setting an unchanged selection does not call it.
- It survives model changes. On clips changed, selected ids among the changed ones are re-queried; on tracks changed, selected tracks are checked against the re-read tracks; on a full reset (and on binding a model) every selected clip is re-queried and the time range is clamped to the new length. Ids whose info is empty are dropped.
- Select all (see Input bindings) selects all clips via `clipsInRange(track, 0, length())` on the selected tracks, or on every track if none is selected; the selected tracks then follow the clips, and the time range is left alone. This is the only full-size query.
- Selected clips are drawn with a tint and an outline in `selectionColourId`; selected tracks tint their header and lane; the time selection shades the ruler's time scale and the tracks.

Keyboard:
- The keyboard cursor (`KeyboardCursor`: an optional track and clip; a clip always has its track) is on a clip, on a track, or nowhere. Its track is the track cursor and its clip the clip cursor, the anchors of Shift + click ranges. Arrows, Return and Escape move the cursor and select what it lands on, replacing the selection; Shift+arrow adds to it. If the cursor does not move, the selection is left alone. The view scrolls to keep the cursor visible: horizontally only as far as needed to show the whole clip with a margin of 5% of the view (less if the clip would not fit with it), or to its start if it is longer than the view and not visible; vertically only as far as needed to show the whole track (its top if it is taller than the view).
- The navigation rules are `navigation::move` in `core/`, reading the model through a `NavigationContext`. `setKeyboardCursor` moves the cursor without selecting or scrolling; `moveKeyboardCursor(move, mode)` does what the keys do. A cursor whose clip or track no longer exists after a model change moves to the clip's track, or nowhere.
- The cursor is drawn (clip inner outline or header outline in `keyboardCursorColourId`) only while the timeline has keyboard focus. Escape with nothing to move is not consumed, so the host can use it.
- With no cursor, any arrow puts it on the first visible clip of the first visible track, or on the first visible track if it has no visible clip.
- On a clip: Left/Right move to the previous/next clip on the same track by start position, ties broken by draw order; at the first or last clip the cursor stays. Up/Down move to the adjacent track (never skipping one): to its clip under the current clip's start, else its nearest visible clip by start, else the track itself. Escape moves the cursor from a clip to its track.
- On a track: Up/Down move to the adjacent track; Right or Enter moves to the track's first visible clip, if any.
- Home/End jump the view to the timeline ends.
- Zoom and track-height keys are listed under Input bindings.

Input bindings:
- Pro Tools defaults. Pro Tools maps Mac Command → Windows Ctrl, Option → Alt, Control → Start (Windows key); Windows and Linux here follow that, except where the Start key would collide with the OS.
- Letter and bracket keys are matched by the character the key produces without modifiers (JUCE's key code), not the typed character: they work on any layout, and Option, which changes the typed character on macOS (Option + A types å), does not break them. Single-letter keys act only while the timeline has keyboard focus.
- The binding logic is in `core/` (`keys::actionFor`, `wheel::actionFor`); `component/` converts JUCE events to it.
- Wheel bindings use whichever wheel delta is non-zero, since macOS turns Shift + wheel into a horizontal delta.

| Action | macOS | Windows / Linux |
|---|---|---|
| Scroll tracks vertically | wheel | wheel |
| Scroll time horizontally | Shift + wheel | Shift + wheel |
| Horizontal zoom, anchored at the zoom anchor | Option + wheel | Alt + wheel |
| Horizontal zoom, anchored at the pointer | Command + wheel | Ctrl + wheel |
| Waveform height zoom (wheel up makes waveforms smaller) | Option + Shift + wheel | Alt + Shift + wheel |
| Horizontal zoom in / out, anchored at the zoom anchor | T / R, Command + ] / [ | T / R, Ctrl + ] / [ |
| Waveform height up / down | Command + Option + ] / [ | Ctrl + Alt + ] / [ |
| Track height up / down | Control + Up / Down | Ctrl + Up / Down |
| Zoom out to show the whole timeline | Option + A | Alt + A |
| Zoom to the selection (`zoomToSelection`) | Option + F | Alt + F |
| Select all clips | Command + A | Ctrl + A |
| Move the keyboard cursor and select | arrows, Return, Escape | arrows, Return, Escape |
| Move the keyboard cursor and add to the selection | Shift + arrows | Shift + arrows |
| Select clip or track / range from the cursor / toggle | click / Shift + click / Command + click | click / Shift + click / Ctrl + click |
| Rubber band adding to the selection | Shift + drag (anywhere) | Shift + drag (anywhere) |
| Play / stop (`onPlayStop`; the application plays) | Space | Space |

- Track height changes apply to the selected tracks, else the keyboard cursor's track, else nothing, and keep the cursor's track in view.
- Each wheel notch (or equivalent trackpad delta) zooms horizontally by a factor of 2^(1/2); each key press by 2. A notched wheel counts one notch per event, since platforms report very different delta sizes per notch (JUCE gives about 0.04 on macOS, 0.2 on Linux, 0.23 on Windows). A smooth (trackpad) delta of 0.125, about 64 points on macOS, counts as one notch.
- Wheel scrolling moves 60 px per notch; smooth scrolling follows the fingers (a JUCE delta of 1 is 512 px).
- On macOS, wheel events with Control held do nothing (Control is not part of any binding there).
- The binding logic is in `core/` (`wheel::actionFor`), independent of components.
- macOS assigns Control + Up/Down to Mission Control and App Exposé by default, so the app never receives them unless the user disables those shortcuts (as Pro Tools users usually do). The README says so.

Look and feel: all colours and metrics through a `LookAndFeel` extension class with defaults, following JUCE conventions. The modules do not follow the system light or dark appearance; the host application chooses the colour scheme.
- `TimelineLookAndFeelMethods`: virtual methods with default implementations, so a host LookAndFeel also derives from it and overrides what it needs; a look and feel that does not derive from it gets the defaults. Methods: `getTimelineMetrics` (ruler height 28, header width 160, scroll bar 12, default track height 72, track height steps, detail thresholds, clip inset 1 px, minimum waveform height 12 px, also used as the stacked-channel minimum, marker strip height 18), `drawTimelineLane` (tinted for selected tracks), `drawTimelineClip(ClipPaint)` (which calls `drawTimelineClipWaveform` and, at the full tier, `drawTimelineClipFades`), `drawTimelineCoverage`, `drawTimelineRuler(marks)` (the time scale), `drawTimelineMarkerStrip(area, MarkerPaint list, optional timeline selection rectangle)`, `drawTimelineTimeSelection(area, inRuler)`, `drawTimelineRubberBand`, `drawTimelinePlayhead` (clip area and the whole ruler), `drawTimelineHighlight`, `drawTimelineTrackHeader` (showing selection and the keyboard cursor through the timeline's getters).
- `ClipPaint` carries the clip's id and info, cut bounds and cut flags, tier, resolved colour, uncut edges (`startX`, `endX`, as `double`), a `ClipWaveform` (the peaks, shared and possibly partial; status; source span across the bounds; the track's waveform scale; the envelope), `selected`, and `cursor` (the keyboard cursor is on it and the timeline has focus). `MarkerPaint` carries the marker's x, the marker, and its resolved colour.
- `Timeline::ColourIds` (`0x2a52000`–`0x2a52012`): background, lane, separator, header background, header text, ruler background, ruler text, clip (default clip colour), clip text, clip outline, playhead, highlight, waveform (default: clip text at 85% alpha), fade shading (default black at 30% alpha), selection (default label text), time selection (default `Slider::thumbColourId`), marker (default orange `0xffe8a33d`), timeline selection (default green `0xff5cb85c`), keyboard cursor (default as the playhead). Each resolves through the timeline, its ancestors and the look and feel, then falls back to blends of `ResizableWindow::backgroundColourId` and `Label::textColourId`, `Slider::thumbColourId` (clip, highlight), and `TextEditor::focusedOutlineColourId` (playhead).

Accessibility:
- JUCE accessibility is component-based, and only focus containers have accessibility children (everything else is flattened into the nearest focus container, ordered by position). The tree is therefore built explicitly: the `Timeline` (role group, titled "Timeline") is a focus container whose traverser lists, for each visible track in display order, its header and then its lane group, followed by the scroll bars. A lane group (`LaneProxy`, role list, titled "<track name> clips", a focus container covering the visible part of the lane) holds the track's clip proxies, left to right, and exposes them as the rows of a one-column table interface (as JUCE's `ListBox` does), since macOS presents lists as outlines that VoiceOver enters through their rows; it posts a structure-changed event when its rows change; a track with no exposed clips has no lane group. So a screen-reader user moves header → that track's clips → next header, and can step out of a track's clips to its next sibling. The clip area itself is an ignored element.
- The clip area keeps a pool of lane groups, each with a pool of proxy components (role list item), one per visible clip, recycled on scroll and zoom. Proxies are visible (JUCE leaves invisible components out of the accessibility tree) but paint nothing, take no keyboard focus and pass all mouse events through (`setInterceptsMouseClicks(false, false)`). Their bounds are the clip's visible part of its lane. Each is titled with the clip name ("Untitled clip" if none), described as "track, start to end" (plus ", group" and ", muted" where they apply), and reports itself selectable and whether it is selected. Accessibility focus never changes the selection, so a screen-reader user can browse a multi-selection without losing it. The press action selects the clip alone; the toggle action adds it to or removes it from the selection.
- Proxies exist only for clips at the reduced or full tier on tracks that are not dense, at most 1000 (visiting tracks top to bottom), and are updated asynchronously after scrolling, zooming, layout and model changes. JUCE gives no public way to tell whether a screen reader is running, so they are kept up to date regardless; updating 1000 proxies costs about 2 ms.
- Tracks are exposed through their header components (role group, titled with the track name, focus containers so custom controls such as mute and solo are their children), with the same selected state and press/toggle actions. The ruler is not exposed.
- Off-screen clips are not exposed; they are reached by keyboard navigation, which scrolls them into view.
- Keyboard cursor moves announce the clip or track (name, description, ", selected"); select all and rubber bands announce the number of clips selected; ruler drags announce the time selected. Each platform to be tested manually.

## Editing (later)

Edits are requests to the model, never applied by the view. The model validates, applies and notifies; the view updates only on notification. Optional callbacks, absent callbacks disable the matching gesture:
- `moveClips(moves)` where each move is clip id, new track, new start
- `trimClips(trims)` (start/end edge drags, adjusting offset). The view clamps trims so that `0 <= offset` and `offset + length <= source length` (an end edge can reach the source's last sample), with length at least 1 and start at least 0. A clip without a source length is clamped only at offset 0.
- `splitClip(id, at)`, `deleteClips(ids)`, `duplicateClips(ids, offset)`
- `setFades(id, in, out)`
- `addClip(track, start, source)` for drops from outside (file drag and drop)
- `undo()`, `redo()`, `beginBatch()`, `endBatch()` as in juce-grid

View-owned editing behaviour: drag ghost and preview, snapping (to grid, clip edges, markers, playhead; configurable and toggled by modifier), ripple and edge modes as options, drag between tracks, auto-scroll while dragging. Snapping logic lives in `core/` and is unit tested.

## Performance

- Cost scales with what is visible, not with the timeline size.
- Track and clip geometry cached per layout pass and invalidated by notifications.
- Waveforms never read audio on the message thread. Peak drawing is O(pixels), not O(samples).
- Targets (to confirm by benchmark): smooth scroll and zoom with 100 tracks and 20,000 clips, hour-long sources, at every zoom level from fully zoomed in to the whole timeline, with an overview strip of the same model open alongside.

## Threading

- Everything GUI runs on the message thread. Peak building and raw sample fetching run on the cache's pool and signal progress through the message thread.
- The model's callbacks are called from the message thread only, except the `WaveformSource` reader factory (background threads).
- The model calls `changed(Change)` on the message thread only.

## Architecture and testing

- `core/`: time mapping (`TimeMapping` for samples ↔ pixels, `ZoomLimits`, `ClipTiming` for timeline ↔ source positions), ruler tick generation and formatting, clip waveform envelopes and status, hit testing, snapping, `Selection`, keyboard navigation (`navigation::move`, `adjacentClip` with its fallback, `firstClipIn`), key bindings, `Change` handling. No component dependencies where possible.
- `component/`: `Timeline`, `TrackHeader`, `Ruler`, `ClipArea`, accessibility proxies (`ClipProxy`, `LaneProxy`, `TimelineTraverser`) and friends. `selectModeFrom(ModifierKeys)` maps click modifiers to a `SelectMode`.
- `demo/timeline/`: the timeline app: a vector-backed model (`DemoModel`, sorted per-track index) with scenarios (16 tracks over an hour plus a track of failure examples: offline, 44.1 kHz and read-error sources; a 100-track 20,000-clip stress test; a 2-second timeline for deep zoom; empty tracks). Audio clips use six synthetic 10-minute stereo sources (`demo/shared/SyntheticSource.h`, shared with the waveform demo) at random offsets, with random gain and fades, optionally slow to build; audio files can be added as tracks by button or drag and drop. Both views share one 512 MB cache. Each scenario but Empty has markers (12 named markers in Session, 1000 in Stress, some coloured) and a timeline selection, and the model answers `adjacentClip` from its sorted index. It also has random clip edits and track add/remove that go through the notifier, playback with page follow, a time or samples ruler, an overview strip moving the main view, zoom to selection, and a status line showing the selection, keyboard cursor and timeline selection. `--scenario <n>`, `--visible <start s> <end s>`, `--select <start s> <end s>` (time selection, the clips in it on tracks 3–6, and track 7) and `--snapshot <png>` serve visual checks. `--snapshot` waits for peak builds to finish.
- `demo/waveform/`: the standalone jwaveform app: a synthetic hour-long stereo source (optionally slow to build), file open and drag and drop, a main view and an overview strip sharing one cache, and controls for channel mode, gain, fades and waveform zoom. `--snapshot <png>` waits for the build, saves a picture of the window and quits.
- `tests/`: Catch2, plus a module check that builds both modules through `juce_add_module`; component tests under a headless message manager where needed. Include partial-`PeakData` reads concurrent with a build under TSan.
- Docs per the project convention: SPEC.md (this file), DECISIONS.md, README.md.

## Milestones

1. Skeleton: modules, CMake, demo app, time mapping with tests. The CI workflow file is written but not run until a repository exists.
2. jwaveform: PeakData, PeakBuilder, PeakCache, drawing; standalone demo.
3. Timeline view: tracks, clips (plain blocks), level-of-detail tiers and overview mode, ruler, virtual scrolling, zoom, playhead.
4. Waveforms in clips, progressive loading, clip fades and gain scaling.
5. Selection, keyboard, accessibility, markers and timeline selection.
6. Deep zoom: `SampleCache`, raw-sample drawing, zoom to 16 px per sample.
7. First Windows and Linux CI run, fix portability issues.
8. Editing: move, trim, split, delete, with snapping.
9. Editing: drag and drop in, duplicate, fade handles, undo forwarding.
10. On-disk peak cache.

## Out of scope

Audio playback, recording, mixing, automation lanes, MIDI clips, time stretching, sample-rate conversion, plugin hosting. The view shows and requests; the application does the rest.
