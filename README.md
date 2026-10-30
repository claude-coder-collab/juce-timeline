# JUCE Timeline

Two [JUCE 8](https://juce.com) modules for showing an audio timeline: tracks of clips with waveform previews, bound to your own model.

- **jwaveform**: waveform peaks and drawing. It works without a timeline.
- **jtimeline**: the timeline view. It depends on jwaveform.

Status: milestone 5. The timeline view shows tracks and clips with waveforms (drawn while they build), gain and fades, a ruler with markers and a loop range, zoom, scrolling, a playhead and an overview mode. Clips, tracks and time ranges can be selected by mouse and keyboard, and visible clips are exposed to screen readers. Zooming past one peak per pixel arrives in milestone 6 (see `SPEC.md`).

### jtimeline

Describe your data as a `TimelineModel` of callbacks and report changes through a `TimelineNotifier`:

```cpp
jtimeline::TimelineNotifier notifier;                    // owned by your model
jtimeline::Timeline timeline;
timeline.setModel({
    .trackIds = [&] { return myTrackIds(); },            // display order
    .trackInfo = [&](jtimeline::TrackId id) { return myTrackInfo(id); },
    .clipsInRange = [&](jtimeline::TrackId id, auto start, auto end) { return myClips(id, start, end); },  // draw order
    .clipInfo = [&](jtimeline::ClipId id) { return myClipInfo(id); },
    .sampleRate = [&] { return 48000.0; },
    .length = [&] { return myLength(); },
    .clipSpans = [&](jtimeline::TrackId id, auto start, auto end, auto minGap) { return mySpans(id, start, end, minGap); },
    .notifier = &notifier,
});
notifier.changed(jtimeline::ClipsChanged{ .ids = { movedClip } });
timeline.onTimeClicked = [&](jtimeline::SamplePosition position) { timeline.setPlayhead(position); };
timeline.onSelectionChanged = [&] { showInspector(timeline.getSelectedClips()); };
```

Clips draw waveforms when their `ClipInfo::source` is set and the timeline has a cache: `timeline.setPeakCache(cache)`, sharing one cache between views (see jwaveform below). Missing files, read errors and sources at another sample rate show as labelled clips.

`clipsInRange` should not scan the whole track; keep clips sorted by start. `clipSpans` and `adjacentClip` are optional but keep zoomed-out views and keyboard navigation fast. Add `markersInRange` and `loopRange` to show markers and a loop range; report their changes with `MarkersChanged`.

Selection is per view and kept by id across model changes. Click clips or track headers (Shift adds, Command/Ctrl toggles), drag over empty space for a rubber band, or drag on the ruler for a time range; arrows, Return and Escape move a keyboard cursor between clips and tracks. For an overview strip, bind a second `Timeline` to the same model and call `setOverviewMode(true)`.

Customise headers with `setTrackHeaderFactory` and a `TrackHeader` subclass, drawing by deriving your LookAndFeel from `jtimeline::TimelineLookAndFeelMethods`, and colours with `Timeline::ColourIds`.

Bindings follow Pro Tools. On macOS, Control + Up/Down (track height) only reaches the app if Mission Control's shortcuts for them are turned off.

### jwaveform

```cpp
auto cache = std::make_shared<jwaveform::PeakCache>();   // share between views
jwaveform::WaveformView view;
view.setPeakCache(cache);
view.setSource({ .key = path + modificationTime,
                 .createReader = [&formats, path] { return std::unique_ptr<juce::AudioFormatReader>{ formats.createReaderFor(juce::File{ path }) }; } });
view.setVisibleRange(startSample, endSample);            // empty range = whole source
```

The reader factory runs on background threads and must return a new reader per call. For custom drawing, use `jwaveform::drawWaveform` with a `PeakHandle` from `PeakCache::request`.

## Using it

```cmake
juce_add_module(path/to/juce-timeline/modules/jwaveform)
juce_add_module(path/to/juce-timeline/modules/jtimeline)
target_link_libraries(MyApp PRIVATE jtimeline jwaveform)
```

Projects using the modules need C++23.

## Building the demo and tests

Requirements: CMake 3.28+, Ninja, and a C++26 compiler (GCC 14, a recent Clang or Apple Clang, or Visual Studio 2022). JUCE 8 and Catch2 are fetched automatically. On Linux, JUCE needs the usual ALSA, X11, FreeType and Fontconfig development packages; see `.github/workflows/ci.yml`.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`demo/waveform` and `demo/timeline` are the demo apps. `python3 tools/check.py` runs every local check: formatting, the tests, ASan/UBSan, TSan and clang-tidy.
