#include "jtimeline_ClipWaveform.h"

#include <algorithm>
#include <cmath>

namespace jtimeline
{

jwaveform::WaveformEnvelope envelopeFor(const ClipInfo& info)
{
    const auto range = info.range();
    const auto length = range.length();
    const auto fadeIn = std::clamp<SamplePosition>(info.fadeIn, 0, length);
    const auto fadeOut = std::clamp<SamplePosition>(info.fadeOut, 0, length);
    const auto sourceEnd = info.sourceOffset + length;
    return { .gainDb = std::isfinite(info.gainDb) ? info.gainDb : 0.0,
        .fadeIn = { .start = info.sourceOffset, .end = info.sourceOffset + fadeIn },
        .fadeOut = { .start = sourceEnd - fadeOut, .end = sourceEnd } };
}

WaveformStatus waveformStatusFor(jwaveform::PeakState state, const jwaveform::PeakData* data, double timelineRate)
{
    if (state == jwaveform::PeakState::none)
        return WaveformStatus::none;
    if (data != nullptr && std::abs(data->getSampleRate() - timelineRate) > 1.0e-6)
        return WaveformStatus::rateMismatch;
    switch (state)
    {
        case jwaveform::PeakState::loading:
            return WaveformStatus::loading;
        case jwaveform::PeakState::ready:
            return WaveformStatus::ready;
        case jwaveform::PeakState::cannotOpen:
            return WaveformStatus::cannotOpen;
        case jwaveform::PeakState::readError:
            return WaveformStatus::readError;
        case jwaveform::PeakState::none:
            break;
    }
    return WaveformStatus::none;
}

bool hasWaveform(const ClipInfo& info)
{
    return info.kind == ClipKind::audio && info.source.has_value() && (info.source->peaks != nullptr || (info.source->key.isNotEmpty() && info.source->createReader));
}

}
