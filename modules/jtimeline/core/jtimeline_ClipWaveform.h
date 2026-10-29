#pragma once

#include "jtimeline_TimelineModel.h"

#include <jwaveform/core/jwaveform_PeakColumns.h>

namespace jtimeline
{

/** What a clip can show of its waveform. */
enum class WaveformStatus {
    none, /**< No waveform: no source, no peak cache, or the clip is drawn without one. */
    loading, /**< Peaks are being built; any peaks built so far are drawn. */
    ready, /**< The peaks are complete. */
    cannotOpen, /**< The source could not be opened, for example a missing file. */
    readError, /**< Reading failed part way; the peaks read before the failure are drawn. */
    rateMismatch /**< The source's sample rate differs from the timeline's; nothing is drawn. */
};

/** Returns the gain and fades a clip's waveform is drawn with, in source positions; fades are clamped to the clip. */
[[nodiscard]] jwaveform::WaveformEnvelope envelopeFor(const ClipInfo& info);

/** Returns a clip's waveform status from its peaks' state and data, given the timeline sample rate. */
[[nodiscard]] WaveformStatus waveformStatusFor(jwaveform::PeakState state, const jwaveform::PeakData* data, double timelineRate);

/** Returns true for a clip that draws a waveform when it has room: an audio clip with a source. */
[[nodiscard]] bool hasWaveform(const ClipInfo& info);

}
