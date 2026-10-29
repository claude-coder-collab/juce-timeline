# JUCE Timeline

Two [JUCE 8](https://juce.com) modules for showing an audio timeline: tracks of clips with waveform previews, bound to your own model.

- **jwaveform**: waveform peaks and drawing. It works without a timeline.
- **jtimeline**: the timeline view. It depends on jwaveform.

Status: milestone 2. jwaveform is usable: progressive peak building, a shared memory-bounded cache and waveform drawing. The timeline view arrives in later milestones (see `SPEC.md`).

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
