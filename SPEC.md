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
- **Clip kind**: `audio` (the default) or `group`. A group clip stands for a group of clips the model keeps elsewhere; the view does not model or draw its contents. It has a span, name and colour like any clip but no source, so it never has a waveform or fades; its body uses a look-and-feel group style (e.g. a distinct outline and a group badge). Empty groups are drawn the same way. Selection, hit testing, level of detail, accessibility and edit requests treat it as a clip; what an edit means for its contents is the model's business.
- **Time unit**: signed 64-bit sample positions at a timeline sample rate supplied by the model. Timeline positions and clip lengths are in timeline samples. Sub-sample positions are not represented. The view may scroll before 0 (pre-roll) but no clip starts there. The default display is hours:minutes:seconds.milliseconds; SMPTE timecode and bars/beats come later (roadmap).
- **Source rate**: every source is at the timeline sample rate, so a clip's source offset and source length are in timeline samples, and a timeline position `p` inside a clip is source sample `sourceOffset + (p - start)`. Converting sources is the model's job. A waveform source whose sample rate differs is shown as a failure state (rate mismatch), not drawn.
- **Identity**: tracks and clips are identified by stable opaque ids (`TrackId`, `ClipId`, 64-bit), not indices, because clips move between tracks and reorder freely. Track order is the model's. Unlike juce-grid, nothing in the API is index-based except track order.

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
- `clipSpans(TrackId, rangeStart, rangeEnd, minGap)` → the occupied spans of the track within the range, merged where clips overlap or the gap between them is smaller than `minGap` samples, sorted by start. Lets coarse drawing (see Level of detail) avoid listing every clip.
- Optional extras for later: markers, loop range.

Reads happen only for visible tracks and clips, plus selected ids when checking they still exist.

Model pushes to the view: a `TimelineNotifier` owned by the model, one `changed(Change)` call, always on the message thread. `Change` is a variant: tracks changed (added, removed, reordered), clips changed (a set of ids, covering add, remove, move and property edits), markers changed, or full reset. A change of `sampleRate()` or `length()` is notified as a full reset. No dirty flag.

On clips changed, the view re-queries each id. It repaints the clip's old rectangle (from its per-layout geometry cache, so a clip that was off screen needs no repaint) and its new one. An id whose `clipInfo` is empty is treated as removed. Because `ClipInfo` carries no draw-order position, every track touched by a changed id (its old track from the cache and its new one from `clipInfo`) has `clipsInRange` called again for the visible range, refreshing the cached draw order used for painting and hit testing.

Lifetime: as juce-grid. The view never owns the model; a destroyed notifier detaches its views.

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
- The cache is owned by the application and handed to each timeline as `timeline.setPeakCache(std::shared_ptr<PeakCache>)`, so several views (e.g. an overview and a main view) share one cache and build each source once. Without a cache, clips draw without waveforms.
- `WaveformSource` (what a clip hands to the view): a key plus a reader factory, or a ready `PeakData`. The model owns file management; jwaveform never decides where audio lives.
- The reader factory is called on background threads, never on the message thread, and must be thread-safe. It is the one model-supplied callback exempt from the message-thread rule. Each call returns a new reader, so readers are never shared between threads.

Deep zoom (milestone 6):
- When the view shows fewer source samples per pixel than level 0 holds, the drawing needs raw samples. A `SampleCache` (owned by `PeakCache`, same memory bound) fetches the visible span of raw samples through the reader factory on the pool, in fixed-size blocks with LRU eviction.
- Until the raw samples arrive, level 0 is drawn stretched. A source that is only a ready `PeakData` (no reader) always draws level 0 stretched.
- With raw samples, the waveform draws as a line through the samples, with a dot per sample once samples are several pixels apart.

Drawing:
- `drawWaveform(Graphics, area, PeakData, sourceStart, sourceEnd, WaveformStyle)` paints a source span (fractional sample positions) across a rectangle, computing only the part inside the graphics clip region, one column per physical pixel, each filled from its min to its max (at least one physical pixel tall). Columns with no data yet are left empty. `WaveformStyle` holds channel mode, minimum lane height, display scale, envelope (gain in dB and linear fade-in/fade-out source ranges, evaluated at each column's centre) and colours. The column computation (`computeColumns`) is in `core/` and tested without graphics.
- `WaveformView`: a component showing a source span (empty span = whole source) through a `PeakCache`, repainting on handle callbacks, with text for loading-without-peaks and failure states. Colour ids: background (default transparent), waveform (default `Slider::thumbColourId`), centre line (default label text at 25% alpha) and text (default `Label::textColourId`), resolved through the component, its parents, then the look and feel. Drawing picks the coarsest level that still has at least one peak per physical pixel (logical width × `Graphics::getInternalContext().getPhysicalPixelScaleFactor()`), so waveforms stay sharp on high-DPI displays; cost is O(physical pixels).
- Channel modes: stacked (one lane per channel), or combined (one lane showing the min of the mins and the max of the maxes across channels). There is no summed mode, as a sum can't be derived from min/max pairs. Stacked falls back to combined when a lane would be shorter than a look-and-feel minimum height (default 12 px).
- A vertical display scale (linear factor, default 1), separate from gain: it multiplies amplitudes. Amplitudes beyond the lane are clamped to its edge.
- Optional gain scaling (gain in dB, converted to linear for drawing) and gain envelope (fade curves multiplied in, so the waveform shrinks through a fade), colours from the look and feel. Amplitude is linear.
- Repaints when the cache reports progress, rate-limited to one repaint per view per 30 ms. A repaint covers only the union of the cached rectangles of visible clips using the affected key, never the whole clip area.
- Failure (missing file, unreadable) is reported as a state the clip can show, not an exception.

## View behaviour (read-only, v1)

Rendering:
- The view is not built on `juce::Viewport`. Scrolling is virtual: the view state is a `double` samples-per-pixel and a start position made of an `int64` sample plus a `double` fraction in [0, 1), driven by `juce::ScrollBar`s. The fraction keeps zoom anchoring exact at deep zoom, where one sample spans many pixels. A content-sized component would exceed `int` range at deep zoom on long timelines.
- Pixel positions are computed relative to the view start in `double`, and clamped to the visible area plus a small margin before conversion to `float`, so graphics coordinates stay small.
- Clips are painted by the clip area, not represented by one component each. Per-clip components exist only as accessibility proxies (see Accessibility).

Layout:
- Track headers on the left (name, colour; contents customisable by a component factory lambda for mute/solo/arm buttons). Header components exist only for visible tracks, kept in a pool and rebound to a different `TrackId` as tracks scroll in and out; the factory is called only when the pool needs another component, and a rebind callback lets custom contents update for the new track. Time ruler on top. Clip area fills the rest.
- Track heights per track, with a global default. Keyboard height changes step through a look-and-feel list of heights (default 24, 32, 48, 72, 112, 176, 272 px); a track between steps moves to the next step in that direction. Vertical scroll for tracks; horizontal scroll for time.
- Playhead drawn from `setPlayhead(samples)`; the view never plays audio. Only the old and new playhead strips are repainted. Optional page follow mode: when the playhead leaves the visible range, the view jumps so the playhead is at its left edge. There is no smooth-scroll follow.
- Optional loop range and markers drawn on the ruler.

Zoom and scroll:
- Zoom is samples-per-pixel (`double`; below 1 at deep zoom).
- **Edit cursor**: the start of the time selection if there is one, otherwise the playhead. It is the anchor for zooms that are not anchored at the pointer: the anchor's screen position stays fixed, or if it is off screen, the view centres on it.
- Bindings follow Pro Tools defaults (see Input bindings). Trackpad horizontal deltas scroll horizontally directly; pinch (`mouseMagnify`) zooms around the pointer.
- API: `setVisibleRange`, `zoomToFit`, `zoomToSelection`.
- Limits: from 1/16 samples per pixel (16 px per sample; until deep zoom lands in milestone 6, the level-0 block size per physical pixel) up to the whole timeline fitting in the view. For zoom-out purposes the timeline is at least 10 seconds long, so an empty timeline still has a range.

Ruler:
- Default formatter: `h:mm:ss.mmm`, dropping leading zero units where zoom allows, and adding sub-millisecond digits (down to microseconds) when ticks are closer than a millisecond. At sample-level zoom, minor ticks mark individual samples. Negative positions are prefixed with `-`.
- A custom formatter lambda replaces the default (applications supply samples, timecode or bars and beats this way). Tick density adapts to zoom.

Clips:
- Drawn as rounded blocks with name, colour, waveform and fades as supplied. Clip gain scales the waveform but is not drawn as a line. Fades are linear in v1, drawn as an overlay shape and applied to the waveform envelope. Muted clips are dimmed.
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
- Hit testing and selection work on individual clips at every tier. A click in a coverage strip hits the topmost clip under the pointer, found by `clipsInRange` on a one-pixel range.
- **Overview mode** (`setOverviewMode(true)`), for an overview strip: every track is drawn in coverage form only, with no per-clip queries, waveforms, headers or clip selection. The view draws a visible-range rectangle set by `setHighlightedRange`, and reports clicks and drags as time positions through a callback, so the application can move another view.

Selection:
- Selection is object-based (clips and tracks), not Pro Tools' range-on-tracks model; only scrolling and zooming follow Pro Tools.
- Clips (multi-select by click, Shift, Ctrl/Cmd, rubber-band), tracks (by header), and a time range (drag on the ruler). Selection is view state, reported through callbacks and held by id.
- It survives model changes. On a clips-changed or tracks-changed notification the affected selected ids are re-queried, and on a full reset all selected ids are; ids whose info is empty are dropped.
- Select all (see Input bindings) selects all clips via `clipsInRange(track, 0, length())` on every track. This is the only full-size query.

Keyboard:
- The keyboard cursor is on a clip, on a track, or nowhere. Arrows move the cursor and select what it lands on, replacing the selection; Shift+arrow adds to it. The view scrolls to keep the cursor visible.
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
| Horizontal zoom, anchored at the edit cursor | Option + wheel | Alt + wheel |
| Horizontal zoom, anchored at the pointer | Command + wheel | Ctrl + wheel |
| Waveform height zoom (wheel up makes waveforms smaller) | Option + Shift + wheel | Alt + Shift + wheel |
| Horizontal zoom in / out, anchored at the edit cursor | T / R, Command + ] / [ | T / R, Ctrl + ] / [ |
| Waveform height up / down | Command + Option + ] / [ | Ctrl + Alt + ] / [ |
| Track height up / down | Control + Up / Down | Ctrl + Up / Down |
| Zoom out to show the whole timeline | Option + A | Alt + A |
| Select all clips | Command + A | Ctrl + A |

- Track height changes apply to the selected tracks, else the keyboard cursor's track, else nothing.
- Each wheel notch (or equivalent trackpad delta) zooms horizontally by a factor of 2^(1/2); each key press by 2. A notched wheel counts one notch per event, since platforms report very different delta sizes per notch (JUCE gives about 0.04 on macOS, 0.2 on Linux, 0.23 on Windows). A smooth (trackpad) delta of 0.125, about 64 points on macOS, counts as one notch.
- Wheel scrolling moves 60 px per notch; smooth scrolling follows the fingers (a JUCE delta of 1 is 512 px).
- On macOS, wheel events with Control held do nothing (Control is not part of any binding there).
- The binding logic is in `core/` (`wheel::actionFor`), independent of components.
- macOS assigns Control + Up/Down to Mission Control and App Exposé by default, so the app never receives them unless the user disables those shortcuts (as Pro Tools users usually do). The README says so.

Look and feel: all colours and metrics through a `LookAndFeel` extension class with defaults, following JUCE conventions. The modules do not follow the system light or dark appearance; the host application chooses the colour scheme.

Accessibility:
- JUCE accessibility is component-based, so the clip area keeps a pool of proxy components, one per visible clip, recycled on scroll and zoom. Proxies are visible (JUCE leaves invisible components out of the accessibility tree) but paint nothing and pass all mouse events through (`setInterceptsMouseClicks(false, false)`). Each exposes name, track, time range and selection state. Accessibility focus never changes the selection, so a screen-reader user can browse a multi-selection without losing it. The press action selects the clip alone; the toggle action adds it to or removes it from the selection.
- Tracks are exposed through their header components.
- Off-screen clips are not exposed; they are reached by keyboard navigation, which scrolls them into view.
- Selection changes are announced. Each platform to be tested manually.

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

- `core/`: time mapping (`TimeMapping` for samples ↔ pixels, `ZoomLimits`, `ClipTiming` for timeline ↔ source positions), ruler tick generation and formatting, hit testing, snapping, selection, keyboard navigation order, `Change` handling. No component dependencies where possible.
- `component/`: `Timeline`, `TrackHeader`, `Ruler`, `ClipArea`, accessibility proxies and friends.
- `demo/timeline/`: the timeline app (synthetic clips and real files, and a stress-test mode, as the view grows).
- `demo/waveform/`: the standalone jwaveform app: a synthetic hour-long stereo source (optionally slow to build), file open and drag and drop, a main view and an overview strip sharing one cache, and controls for channel mode, gain, fades and waveform zoom. `--snapshot <png>` waits for the build, saves a picture of the window and quits.
- `tests/`: Catch2, plus a module check that builds both modules through `juce_add_module`; component tests under a headless message manager where needed. Include partial-`PeakData` reads concurrent with a build under TSan.
- Docs per the project convention: SPEC.md (this file), DECISIONS.md, README.md.

## Milestones

1. Skeleton: modules, CMake, demo app, time mapping with tests. The CI workflow file is written but not run until a repository exists.
2. jwaveform: PeakData, PeakBuilder, PeakCache, drawing; standalone demo.
3. Timeline view: tracks, clips (plain blocks), level-of-detail tiers and overview mode, ruler, virtual scrolling, zoom, playhead.
4. Waveforms in clips, progressive loading, clip fades and gain scaling.
5. Selection, keyboard, accessibility, markers and loop range.
6. Deep zoom: `SampleCache`, raw-sample drawing, zoom to 16 px per sample.
7. First Windows and Linux CI run, fix portability issues.
8. Editing: move, trim, split, delete, with snapping.
9. Editing: drag and drop in, duplicate, fade handles, undo forwarding.
10. On-disk peak cache.

## Out of scope

Audio playback, recording, mixing, automation lanes, MIDI clips, time stretching, sample-rate conversion, plugin hosting. The view shows and requests; the application does the rest.
