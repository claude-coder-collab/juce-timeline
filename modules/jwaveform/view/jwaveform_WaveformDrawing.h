#pragma once

#include "../core/jwaveform_PeakColumns.h"

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
};

/**
    Draws the source span [sourceStart, sourceEnd) of data into area, which it fills horizontally.
    Only the part of area inside the graphics clip region is computed, one column per physical
    pixel, so the cost is proportional to the visible physical width.
*/
void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area, const PeakData& data, double sourceStart, double sourceEnd, const WaveformStyle& style);

/** Returns the lanes drawWaveform() uses for a number of channels in an area: one per channel, or one combined. */
[[nodiscard]] int laneCountFor(int numChannels, float areaHeight, const WaveformStyle& style);

}
