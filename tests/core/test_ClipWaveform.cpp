#include <jtimeline/core/jtimeline_ClipWaveform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using jtimeline::WaveformStatus;
using jwaveform::PeakState;

TEST_CASE("envelopeFor places fades at the clip's source ends", "[waveform]")
{
    jtimeline::ClipInfo info;
    info.start = 1000;
    info.length = 400;
    info.sourceOffset = 5000;
    info.gainDb = -6.0;
    info.fadeIn = 100;
    info.fadeOut = 50;
    const auto envelope = jtimeline::envelopeFor(info);
    CHECK(envelope.gainDb == -6.0);
    CHECK(envelope.fadeIn == jtimeline::SampleRange{ .start = 5000, .end = 5100 });
    CHECK(envelope.fadeOut == jtimeline::SampleRange{ .start = 5350, .end = 5400 });
    CHECK(envelope.gainAt(5050.0) == Approx(0.5 * jwaveform::decibelsToGain(-6.0)));

    info.fadeIn = 1000;
    info.fadeOut = -5;
    info.gainDb = std::numeric_limits<double>::quiet_NaN();
    const auto clamped = jtimeline::envelopeFor(info);
    CHECK(clamped.fadeIn == jtimeline::SampleRange{ .start = 5000, .end = 5400 });
    CHECK(clamped.fadeOut.isEmpty());
    CHECK(clamped.gainDb == 0.0);
}

TEST_CASE("waveformStatusFor reports rate mismatches over the peak state", "[waveform]")
{
    const jwaveform::PeakData matching{ 48000.0, 100, 1 };
    const jwaveform::PeakData other{ 44100.0, 100, 1 };
    CHECK(jtimeline::waveformStatusFor(PeakState::none, &other, 48000.0) == WaveformStatus::none);
    CHECK(jtimeline::waveformStatusFor(PeakState::loading, nullptr, 48000.0) == WaveformStatus::loading);
    CHECK(jtimeline::waveformStatusFor(PeakState::loading, &matching, 48000.0) == WaveformStatus::loading);
    CHECK(jtimeline::waveformStatusFor(PeakState::ready, &matching, 48000.0) == WaveformStatus::ready);
    CHECK(jtimeline::waveformStatusFor(PeakState::ready, &other, 48000.0) == WaveformStatus::rateMismatch);
    CHECK(jtimeline::waveformStatusFor(PeakState::cannotOpen, nullptr, 48000.0) == WaveformStatus::cannotOpen);
    CHECK(jtimeline::waveformStatusFor(PeakState::readError, &matching, 48000.0) == WaveformStatus::readError);
}

TEST_CASE("hasWaveform needs an audio clip with a usable source", "[waveform]")
{
    jtimeline::ClipInfo info;
    CHECK_FALSE(jtimeline::hasWaveform(info));
    info.source = jwaveform::WaveformSource{ .key = "a", .createReader = {}, .peaks = nullptr };
    CHECK_FALSE(jtimeline::hasWaveform(info));
    info.source->createReader = [] { return std::unique_ptr<juce::AudioFormatReader>{}; };
    CHECK(jtimeline::hasWaveform(info));
    info.kind = jtimeline::ClipKind::group;
    CHECK_FALSE(jtimeline::hasWaveform(info));
}
