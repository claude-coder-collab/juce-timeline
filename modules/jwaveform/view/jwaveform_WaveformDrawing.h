#pragma once

#include "../core/jwaveform_PeakColumns.h"
#include "../core/jwaveform_RawSamples.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jwaveform
{

//==============================================================================
/** How drawWaveform() draws. */
struct WaveformStyle
{
    ChannelMode channelMode = ChannelMode::stacked; /**< Lane layout for multi-channel sources. */
    float minLaneHeight = 12.0f; /**< Stacked lanes shorter than this fall back to combined. */
    double displayScale = 1.0; /**< Multiplies amplitudes for display; values past a lane's edge are clamped. */
    WaveformEnvelope envelope; /**< Clip gain and fades, multiplied into the drawn amplitude. */
    juce::Colour colour = juce::Colours::white; /**< The waveform colour. */
    juce::Colour centreLineColour = juce::Colours::transparentBlack; /**< Drawn through each lane's centre. */
    float lineThickness = 1.0f; /**< The thickness of the line through raw samples. */
    float sampleDotSpacing = 6.0f; /**< Raw samples at least this far apart are drawn with a dot. */
    float sampleDotDiameter = 3.0f; /**< The diameter of a sample dot. */
};

/**
    Draws the source span [sourceStart, sourceEnd) of data into area, which it fills horizontally.
    Only the part of area inside the graphics clip region is computed, one column per physical
    pixel, so the cost is proportional to the visible physical width.

    When a physical pixel shows fewer source samples than a level-0 peak covers, raw samples are
    used where they are held: each column then spans the samples it overlaps, and once a sample is
    wider than a physical pixel the waveform is drawn as a line through the samples' centres, with
    a dot per sample when they are at least style.sampleDotSpacing apart. Elsewhere level 0 is
    drawn stretched.
*/
void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area, const PeakData& data, double sourceStart, double sourceEnd, const WaveformStyle& style, const RawSamples& samples = {});

/** Returns the lanes drawWaveform() uses for a number of channels in an area: one per channel, or one combined. */
[[nodiscard]] int laneCountFor(int numChannels, float areaHeight, const WaveformStyle& style);

}
