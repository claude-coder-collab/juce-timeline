# JUCE Timeline — Specification (draft)

A pair of JUCE modules for viewing, and later editing, an audio timeline: tracks containing clips, with waveform previews. Private repository. For now it is local only: there is no GitHub repository yet, so no remote, PRs or hosted CI until one is created. Targets JUCE 8.

## Modules

- **jwaveform**: waveform peaks and drawing. Depends on `juce_audio_formats` and `juce_gui_basics`. Usable on its own, with no timeline.
- **jtimeline**: the timeline view. Depends on `jwaveform` and `juce_gui_basics` (a hard dependency: `ClipInfo` carries jwaveform types). A clip with no waveform source draws as a plain block.
- Module folders are `modules/jwaveform` and `modules/jtimeline`, each with an umbrella header and source named after the folder (`jwaveform.h`, `jwaveform.cpp`), as JUCE requires.
- Both: C++23 minimum, same conventions as juce-grid (`core/` for non-GUI logic, a component folder, one umbrella header per module).

## Platforms and CI

- Windows, macOS, Linux. Platform differences go through JUCE.
- Develop on macOS (arm64). Build with CMake and Ninja Multi-Config.
- Once a GitHub repository exists (not yet), CI is a single manually triggered workflow (`workflow_dispatch` only, no push, PR or schedule triggers) building and testing on Linux (GCC, under xvfb) and Windows (MSVC). Run it once to check portability, and again before a release. Sanitizers and clang-tidy run locally.
- Tests: Catch2, with the model-independent logic (time mapping, hit testing, peak generation, snapping) tested without a display.

## Concepts

- **Timeline**: an ordered list of tracks over a shared time axis.
- **Track**: a lane holding clips. Overlapping clips on one track are allowed and drawn overlapping, in draw order (later on top), with the overlap visible: clip bodies are slightly translucent and each clip keeps its own outline, so the clip underneath can be seen. Hit testing picks the topmost clip.
- **Draw order**: a per-track total order of clips defined by the model. It is the same whichever range is queried.
- **Clip**: a span of time on a track, referring to a region of a source. Has a track, a start, a length of at least 1 sample, a source offset, a name and a colour. Clips lie within `[0, length()]` of the timeline. Optional display-only gain and fades.
- **Time unit**: signed 64-bit sample positions at a timeline sample rate supplied by the model. Timeline positions and clip lengths are in timeline samples. Sub-sample positions are not represented. The view may scroll before 0 (pre-roll) but no clip starts there. The default display is hours:minutes:seconds.milliseconds; SMPTE timecode and bars/beats come later (roadmap).
- **Source rate**: every source is at the timeline sample rate, so a clip's source offset and source length are in timeline samples, and a timeline position `p` inside a clip is source sample `sourceOffset + (p - start)`. Converting sources is the model's job. A waveform source whose sample rate differs is shown as a failure state (rate mismatch), not drawn.
- **Identity**: tracks and clips are identified by stable opaque ids (`TrackId`, `ClipId`, 64-bit), not indices, because clips move between tracks and reorder freely. Track order is the model's. Unlike juce-grid, nothing in the API is index-based except track order.

## Model communication

Same approach as juce-grid: a struct of `std::function` members, no inheritance, the view is not a template. Bound with `timeline.setModel(TimelineModel)`.

View pulls from the model (all required unless marked optional):
- `trackIds()` → ordered list of `TrackId`
- `trackInfo(TrackId)` → `std::optional<TrackInfo>`: name, colour, height hint (optional fields). Empty means the track does not exist.
- `clipsInRange(TrackId, rangeStart, rangeEnd)` → ids of the clips on the track with `clip.start < rangeEnd && clip.start + clip.length > rangeStart`, in draw order. So a clip starting before the range but extending into it is included, and one ending exactly at `rangeStart` is not. The view always passes `0 <= rangeStart < rangeEnd <= length()`, never sentinel extremes. The model is expected to answer this without scanning the whole track (e.g. a sorted index or interval tree); the view never asks for all clips except for select-all.
- `clipInfo(ClipId)` → `std::optional<ClipInfo>`: track, start, length, source offset, source length (optional), name, colour, gain, fade in, fade out, muted, optional `WaveformSource`. Empty means the clip does not exist. A reported length of 0 is a model error; the view treats it as 1.
- `sampleRate()`
- `length()`: the timeline end in samples. The model tracks it; the view never derives it.
- Optional extras for later: markers, loop range, tempo map (for the bars ruler).

Reads happen only for visible tracks and clips, plus selected ids when checking they still exist.

Model pushes to the view: a `TimelineNotifier` owned by the model, one `changed(Change)` call, always on the message thread. `Change` is a variant: tracks changed (added, removed, reordered), clips changed (a set of ids, covering add, remove, move and property edits), markers changed, or full reset. A change of `sampleRate()` or `length()` is notified as a full reset. No dirty flag.

On clips changed, the view re-queries each id. It repaints the clip's old rectangle (from its per-layout geometry cache, so a clip that was off screen needs no repaint) and its new one. An id whose `clipInfo` is empty is treated as removed. Because `ClipInfo` carries no draw-order position, every track touched by a changed id (its old track from the cache and its new one from `clipInfo`) has `clipsInRange` called again for the visible range, refreshing the cached draw order used for painting and hit testing.

Lifetime: as juce-grid. The view never owns the model; a destroyed notifier detaches its views.

Multiple views of one model are supported (e.g. an overview strip and a main view). Each view has its own zoom, scroll, selection and track heights.

## Waveforms (jwaveform)

Data:
- `PeakData`: a multi-resolution min/max pyramid for one audio source, with the source's sample rate, length and channel count. Level 0 is one min/max pair per N samples (default 64) per channel; each higher level halves resolution. Values are `int16` normalised to ±1, so an hour of 48 kHz stereo is about 43 MB including the pyramid (level 0 about 22 MB).
- Storage is chunked in fixed-size blocks that never reallocate once written. Each level has its own atomically published count of valid pairs (release on write, acquire on read), so a reader can draw a partially built `PeakData` while the builder appends. Higher levels are filled as the pairs below them complete; a reader reads each level only up to that level's count, and draws the remainder of the range from the coarsest level that covers it (at worst level 0).
- `PeakData` is shared as `std::shared_ptr<const PeakData>` (the builder holds the only mutable access). Eviction drops the cache's reference; anything drawing keeps its own until done.

Building:
- `PeakBuilder`: builds `PeakData` from an `AudioFormatReader` (or any callback that yields sample blocks) on a background thread, progressively, so partial results can be drawn while analysis continues.
- Builds run on a thread pool owned by the cache (default 2 threads, configurable). A build is cancelled when no handle requests its key any more; finished or partial results stay cached.

Cache:
- `PeakCache`: shared, keyed by a source key supplied by the model (e.g. file path plus modification time). One build per key however many clips use it. Thread-safe.
- Memory-bounded in bytes (default 256 MB, configurable) with LRU eviction. A key with a build in progress is not evicted.
- Optional on-disk cache: off unless the application sets a directory (`setDiskCacheDirectory`); jwaveform never picks a location. One file per source, named by a hash of the key. The header holds a format version, the full key, sample rate, length and channel count; any mismatch means the file is ignored and rebuilt.

Sources:
- `WaveformSource` (what a clip hands to the view): a key plus a reader factory, or a ready `PeakData`. The model owns file management; jwaveform never decides where audio lives.
- The reader factory is called on background threads, never on the message thread, and must be thread-safe. It is the one model-supplied callback exempt from the message-thread rule. Each call returns a new reader, so readers are never shared between threads.

Deep zoom:
- When the view shows fewer source samples per pixel than level 0 holds, the drawing needs raw samples. A `SampleCache` (owned by `PeakCache`, same memory bound) fetches the visible span of raw samples through the reader factory on the pool, in fixed-size blocks with LRU eviction.
- Until the raw samples arrive, level 0 is drawn stretched. A source that is only a ready `PeakData` (no reader) always draws level 0 stretched.
- With raw samples, the waveform draws as a line through the samples, with a dot per sample once samples are several pixels apart.

Drawing:
- `WaveformView`: a lightweight component or free draw function that paints a source sample range of a `PeakData` into a rectangle. Drawing picks the level nearest one peak per pixel, so cost is O(pixels).
- Channel modes: stacked (one lane per channel), or combined (one lane showing the min of the mins and the max of the maxes across channels). There is no summed mode, as a sum can't be derived from min/max pairs. Stacked falls back to combined when a lane would be shorter than a look-and-feel minimum height (default 12 px).
- Optional gain scaling and gain envelope (fade curves multiplied in, so the waveform shrinks through a fade), optional logarithmic amplitude, colours from the look and feel.
- Repaints when the cache reports progress, rate-limited to one repaint per view per 30 ms. A repaint covers only the union of the cached rectangles of visible clips using the affected key, never the whole clip area.
- Failure (missing file, unreadable) is reported as a state the clip can show, not an exception.

## View behaviour (read-only, v1)

Rendering:
- The view is not built on `juce::Viewport`. Scrolling is virtual: the view state is a `double` samples-per-pixel and an `int64` start position, driven by `juce::ScrollBar`s. A content-sized component would exceed `int` range at deep zoom on long timelines.
- Pixel positions are computed relative to the view start in `double`, and clamped to the visible area plus a small margin before conversion to `float`, so graphics coordinates stay small.
- Clips are painted by the clip area, not represented by one component each. Per-clip components exist only as accessibility proxies (see Accessibility).

Layout:
- Track headers on the left (name, colour; contents customisable by a component factory lambda for mute/solo/arm buttons). Time ruler on top. Clip area fills the rest.
- Track heights per track, with a global default. Vertical scroll for tracks; horizontal scroll for time.
- Playhead drawn from `setPlayhead(samples)`; the view never plays audio. Only the old and new playhead strips are repainted. Optional follow mode (page or smooth scroll).
- Optional loop range and markers drawn on the ruler.

Zoom and scroll:
- Zoom is samples-per-pixel (`double`; below 1 at deep zoom).
- Mouse: Ctrl/Cmd + wheel zooms around the pointer; plain wheel scrolls vertically; Shift + wheel scrolls horizontally.
- Trackpad: horizontal deltas scroll horizontally directly; pinch (`mouseMagnify`) zooms around the pointer.
- API: `setVisibleRange`, `zoomToFit`, `zoomToSelection`.
- Limits: from 1/16 samples per pixel (16 px per sample) up to the whole timeline fitting in the view. For zoom-out purposes the timeline is at least 10 seconds long, so an empty timeline still has a range.

Ruler:
- Default formatter: `h:mm:ss.mmm`, dropping leading zero units where zoom allows, and adding sub-millisecond digits (down to microseconds) when ticks are closer than a millisecond. At sample-level zoom, minor ticks mark individual samples. Negative positions are prefixed with `-`.
- Later formatters: samples, SMPTE timecode, bars and beats (needs a tempo map). A custom formatter lambda is accepted. Tick density adapts to zoom.

Clips:
- Drawn as rounded blocks with name, colour, waveform, fades and gain line as supplied. Fades are linear in v1, drawn as an overlay shape and applied to the waveform envelope. Muted clips are dimmed.
- Clip body is drawn by the view; a custom painter lambda may replace it per clip.
- Painting is clipped to the visible range and culls clips narrower than a pixel.

Selection:
- Clips (multi-select by click, Shift, Ctrl/Cmd, rubber-band), tracks (by header), and a time range (drag on the ruler). Selection is view state, reported through callbacks and held by id.
- It survives model changes. On a clips-changed or tracks-changed notification the affected selected ids are re-queried, and on a full reset all selected ids are; ids whose info is empty are dropped.
- Ctrl/Cmd+A selects all clips via `clipsInRange(track, 0, length())` on every track. This is the only full-size query.

Keyboard:
- The keyboard cursor is on a clip, on a track, or nowhere. Arrows move the cursor and select what it lands on, replacing the selection; Shift+arrow adds to it. The view scrolls to keep the cursor visible.
- With no cursor, any arrow puts it on the first visible clip of the first visible track, or on the first visible track if it has no visible clip.
- On a clip: Left/Right move to the previous/next clip on the same track by start position, ties broken by draw order; at the first or last clip the cursor stays. Up/Down move to the adjacent track (never skipping one): to its clip under the current clip's start, else its nearest visible clip by start, else the track itself. Escape moves the cursor from a clip to its track.
- On a track: Up/Down move to the adjacent track; Right or Enter moves to the track's first visible clip, if any.
- Home/End jump the view to the timeline ends.
- Zoom in on the characters `+` or `=`, zoom out on `-`, matched by typed character (`KeyPress::getTextCharacter`) so they work on any layout, plus the numpad `+`/`-` by key code.

Look and feel: all colours and metrics through a `LookAndFeel` extension class with defaults, following JUCE conventions. Light and dark.

Accessibility:
- JUCE accessibility is component-based, so the clip area keeps a pool of invisible proxy components, one per visible clip, recycled on scroll and zoom. Each exposes name, track, time range and selection state. Accessibility focus never changes the selection, so a screen-reader user can browse a multi-selection without losing it. The press action selects the clip alone; the toggle action adds it to or removes it from the selection.
- Tracks are exposed through their header components.
- Off-screen clips are not exposed; they are reached by keyboard navigation, which scrolls them into view.
- Selection changes are announced. Each platform to be tested manually.

## Editing (later)

Edits are requests to the model, never applied by the view. The model validates, applies and notifies; the view updates only on notification. Optional callbacks, absent callbacks disable the matching gesture:
- `moveClips(moves)` where each move is clip id, new track, new start
- `trimClips(trims)` (start/end edge drags, adjusting offset). The view clamps trims so that `0 <= offset` and `offset + length <= source length` (an end edge can reach the source's last sample), with length at least 1 and start at least 0. A clip without a source length is clamped only at offset 0.
- `splitClip(id, at)`, `deleteClips(ids)`, `duplicateClips(ids, offset)`
- `setFades(id, in, out)`, `setGain(id, gain)`
- `addClip(track, start, source)` for drops from outside (file drag and drop)
- `undo()`, `redo()`, `beginBatch()`, `endBatch()` as in juce-grid

View-owned editing behaviour: drag ghost and preview, snapping (to grid, clip edges, markers, playhead; configurable and toggled by modifier), ripple and edge modes as options, drag between tracks, auto-scroll while dragging. Snapping logic lives in `core/` and is unit tested.

## Performance

- Cost scales with what is visible, not with the timeline size.
- Track and clip geometry cached per layout pass and invalidated by notifications.
- Waveforms never read audio on the message thread. Peak drawing is O(pixels), not O(samples).
- Targets (to confirm by benchmark): smooth scroll and zoom with 100 tracks and 20,000 clips, hour-long sources.

## Threading

- Everything GUI runs on the message thread. Peak building and raw sample fetching run on the cache's pool and signal progress through the message thread.
- The model's callbacks are called from the message thread only, except the `WaveformSource` reader factory (background threads).
- The model calls `changed(Change)` on the message thread only.

## Architecture and testing

- `core/`: time mapping (samples ↔ pixels, timeline ↔ source positions), ruler tick generation and formatting, hit testing, snapping, selection, keyboard navigation order, `Change` handling. No component dependencies where possible.
- `component/`: `Timeline`, `TrackHeader`, `Ruler`, `ClipArea`, accessibility proxies and friends.
- `demo/`: an app with synthetic clips and real files, and a stress-test mode.
- `tests/`: Catch2; component tests under a headless message manager where needed. Include partial-`PeakData` reads concurrent with a build under TSan.
- Docs per the project convention: SPEC.md (this file), DECISIONS.md, README.md.

## Milestones

1. Skeleton: modules, CMake, demo app, time mapping with tests. The CI workflow file is written but not run until a repository exists.
2. jwaveform: PeakData, PeakBuilder, PeakCache, drawing; standalone demo.
3. Timeline view: tracks, clips (plain blocks), ruler, virtual scrolling, zoom, playhead.
4. Waveforms in clips, progressive loading, deep-zoom raw samples, clip fades and gain display.
5. Selection, keyboard, accessibility, markers and loop range.
6. First Windows and Linux CI run, fix portability issues.
7. Editing: move, trim, split, delete, with snapping.
8. Editing: drag and drop in, duplicate, fades and gain handles, undo forwarding.

## Out of scope

Audio playback, recording, mixing, automation lanes, MIDI clips, time stretching, sample-rate conversion, plugin hosting. The view shows and requests; the application does the rest.
