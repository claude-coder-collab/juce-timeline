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
