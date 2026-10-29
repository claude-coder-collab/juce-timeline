# JUCE Timeline — Specification (draft)

A pair of JUCE modules for viewing, and later editing, an audio timeline: tracks containing clips, with waveform previews. Private repository. For now it is local only: there is no GitHub repository yet, so no remote, PRs or hosted CI until one is created. Targets JUCE 8.

## Modules

- **juce-waveform** (module ID `juce_waveform`): waveform peaks and drawing. Depends on `juce_audio_formats` and `juce_gui_basics`. Usable on its own, with no timeline. Module IDs use underscores because they must be valid C++ identifiers.
- **juce-timeline** (module ID `juce_timeline`): the timeline view. Depends on `juce_waveform` and `juce_gui_basics`. juce_waveform stays optional at runtime: a clip with no waveform source draws as a plain block.
- Both: C++23 minimum, same conventions as juce-grid (`core/` for non-GUI logic, a component folder, one umbrella header per module).

## Platforms and CI

- Windows, macOS, Linux. Platform differences go through JUCE.
- Develop on macOS (arm64). Build with CMake and Ninja Multi-Config.
- Once a GitHub repository exists (not yet), CI is a single manually triggered workflow (`workflow_dispatch` only, no push, PR or schedule triggers) building and testing on Linux (GCC, under xvfb) and Windows (MSVC). Run it once to check portability, and again before a release. Sanitizers and clang-tidy run locally.
- Tests: Catch2, with the model-independent logic (time mapping, hit testing, peak generation, snapping) tested without a display.

## Concepts

- **Timeline**: an ordered list of tracks over a shared time axis.
- **Track**: a lane holding clips. Overlapping clips on one track are allowed and drawn overlapping, in model order (later on top), with the overlap visible: clip bodies are slightly translucent and each clip keeps its own outline, so the clip underneath can be seen. Hit testing picks the topmost clip.
- **Clip**: a span of time on a track, referring to a region of a source. Has a start, a length, an offset into its source, a name and a colour. Optional display-only gain and fades.
- **Time unit**: signed 64-bit sample positions at a timeline sample rate supplied by the model. The view converts positions for display only. The default display is hours:minutes:seconds.milliseconds; SMPTE timecode and bars/beats come later (roadmap). Sub-sample positions are not represented.
- **Identity**: tracks and clips are identified by stable opaque ids (`TrackId`, `ClipId`, 64-bit), not indices, because clips move between tracks and reorder freely. Track order is the model's. Unlike juce-grid, nothing in the API is index-based except track order.

## Model communication

Same approach as juce-grid: a struct of `std::function` members, no inheritance, the view is not a template. Bound with `timeline.setModel(TimelineModel)`.

View pulls from the model:
- `trackIds()` → ordered list of `TrackId`
- `trackInfo(TrackId)` → name, colour, height hint (optional fields)
- `clipIds(TrackId)` → clips on the track in draw order
- `clipInfo(ClipId)` → `ClipInfo`: start, length, source offset, name, colour, gain, fade in, fade out, muted, optional `WaveformSource`
- `sampleRate()`
- `length()`, optional: the timeline end, otherwise derived from the last clip
- Optional extras for later: markers, loop range, tempo map (for the bars ruler).

Reads happen only for visible tracks and clips. `clipIds` must support range queries so a timeline with tens of thousands of clips does not require a full scan: `clipsInRange(TrackId, start, end)` is optional, falling back to filtering `clipIds`.

Model pushes to the view: a `TimelineNotifier` owned by the model, one `changed(Change)` call. `Change` is a variant: tracks changed (added, removed, reordered), clips changed (a set of ids, covering add, remove, move and property edits), markers changed, or full reset. The view repaints and re-queries only what is visible. No dirty flag.

Lifetime: as juce-grid. The view never owns the model; a destroyed notifier detaches its views.

Multiple views of one model are supported (e.g. an overview strip and a main view). Each view has its own zoom, scroll, selection and track heights.

## Waveforms (juce-waveform)

- `PeakData`: a multi-resolution min/max pyramid for one audio source. Level 0 is one min/max pair per N samples (default 64) per channel; each higher level halves resolution. Drawing picks the level nearest one peak per pixel.
- `PeakBuilder`: builds `PeakData` from an `AudioFormatReader` (or any callback that yields sample blocks) on a background thread, progressively, so partial results can be drawn while analysis continues.
- `PeakCache`: shared, keyed by a source key supplied by the model (e.g. file path plus modification time). Memory-bounded with LRU eviction. Optional on-disk cache file per source, with a versioned format. Thread-safe; one build per key however many clips use it.
- `WaveformSource` (what a clip hands to the view): a key plus a way to open a reader, or a ready `PeakData`. The model owns file management; juce-waveform never decides where audio lives.
- `WaveformView`: a lightweight component or free draw function that paints a sample range of a `PeakData` into a rectangle: channels stacked or summed, optional gain scaling, optional logarithmic amplitude, colours from the look and feel. Repaints when the cache reports progress.
- Failure (missing file, unreadable) is reported as a state the clip can show, not an exception.

## View behaviour (read-only, v1)

Layout:
- Track headers on the left (name, colour; contents customisable by a component factory lambda for mute/solo/arm buttons). Time ruler on top. Clip area fills the rest.
- Track heights per track, with a global default. Vertical scroll for tracks; horizontal scroll for time.
- Playhead drawn from `setPlayhead(samples)`; the view never plays audio. Optional follow mode (page or smooth scroll).
- Optional loop range and markers drawn on the ruler.

Zoom and scroll:
- Zoom is samples-per-pixel. Ctrl/Cmd + wheel zooms around the pointer; plain wheel scrolls; Shift + wheel scrolls horizontally. API: `setVisibleRange`, `zoomToFit`, `zoomToSelection`.
- Limits: from a few pixels per sample up to the whole timeline fitting in the view.

Ruler:
- Default formatter: `h:mm:ss.mmm`, dropping leading zero units where zoom allows. Later formatters: samples, SMPTE timecode, bars and beats (needs a tempo map). A custom formatter lambda is accepted. Tick density adapts to zoom.

Clips:
- Drawn as rounded blocks with name, colour, waveform, fades and gain line as supplied. Muted clips are dimmed.
- Clip body is drawn by the view; a custom painter lambda may replace it per clip.
- Painting is clipped to the visible range and culls clips narrower than a pixel.

Selection:
- Clips (multi-select by click, Shift, Ctrl/Cmd, rubber-band), tracks (by header), and a time range (drag on the ruler). Selection is view state, reported through callbacks and held by id; it survives model changes and drops ids that no longer exist.

Keyboard: arrows move the selection between clips and tracks, Home/End jump to the timeline ends, +/- zoom, Ctrl+A selects all clips.

Look and feel: all colours and metrics through a `LookAndFeel` extension class with defaults, following JUCE conventions. Light and dark.

Accessibility: tracks and clips exposed as accessibility elements with name, time range and selection state; keyboard navigation as above; announcements for selection changes. Each platform to be tested manually.

## Editing (later)

Edits are requests to the model, never applied by the view. The model validates, applies and notifies; the view updates only on notification. Optional callbacks, absent callbacks disable the matching gesture:
- `moveClips(moves)` where each move is clip id, new track, new start
- `trimClips(trims)` (start/end edge drags, adjusting offset)
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

- Everything GUI runs on the message thread. Peak building runs on background threads and signals completion through the message thread.
- The model's callbacks are called from the message thread only.

## Architecture and testing

- `core/`: time mapping (samples ↔ pixels), ruler tick generation, hit testing, snapping, selection, `Change` handling. No component dependencies where possible.
- `component/`: `Timeline`, `TrackHeader`, `Ruler`, `ClipView` and friends.
- `demo/`: an app with synthetic clips and real files, and a stress-test mode.
- `tests/`: Catch2; component tests under a headless message manager where needed.
- Docs per the project convention: SPEC.md (this file), DECISIONS.md, README.md.

## Milestones

1. Skeleton: modules, CMake, demo app, CI workflow, time mapping with tests.
2. juce-waveform: PeakData, PeakBuilder, PeakCache, drawing; standalone demo.
3. Timeline view: tracks, clips (plain blocks), ruler, scrolling, zoom, playhead.
4. Waveforms in clips, progressive loading, clip fades and gain display.
5. Selection, keyboard, accessibility, markers and loop range.
6. First Windows and Linux CI run, fix portability issues.
7. Editing: move, trim, split, delete, with snapping.
8. Editing: drag and drop in, duplicate, fades and gain handles, undo forwarding.

## Out of scope

Audio playback, recording, mixing, automation lanes, MIDI clips, time stretching, plugin hosting. The view shows and requests; the application does the rest.
