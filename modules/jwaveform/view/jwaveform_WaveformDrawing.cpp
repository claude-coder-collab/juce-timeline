#include "jwaveform_WaveformDrawing.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace jwaveform
{

int laneCountFor(int numChannels, float areaHeight, const WaveformStyle& style)
{
    if (style.channelMode == ChannelMode::combined || numChannels <= 1)
        return 1;
    return areaHeight / static_cast<float>(numChannels) < style.minLaneHeight ? 1 : numChannels;
}

void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area, const PeakData& data, double sourceStart, double sourceEnd, const WaveformStyle& style)
{
    if (area.isEmpty() || !(sourceEnd > sourceStart))
        return;
    const auto visible = area.getIntersection(g.getClipBounds().toFloat());
    if (visible.isEmpty())
        return;

    const auto scale = static_cast<double>(g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto pixelScale = scale > 0.0 ? scale : 1.0;
    const auto columnWidth = 1.0 / pixelScale;
    const auto samplesPerPixel = (sourceEnd - sourceStart) / static_cast<double>(area.getWidth());
    const auto samplesPerColumn = samplesPerPixel * columnWidth;
    const auto firstX = std::floor(static_cast<double>(visible.getX() - area.getX()) * pixelScale) * columnWidth;
    const auto columnCount = static_cast<std::size_t>(std::ceil((static_cast<double>(visible.getRight() - area.getX()) - firstX) * pixelScale));
    const auto firstSample = sourceStart + (firstX * samplesPerPixel);

    std::vector<ColumnPeak> columns(columnCount);
    std::vector<float> gains(columnCount);
    for (std::size_t x = 0; x < columnCount; ++x)
        gains[x] = static_cast<float>(style.envelope.gainAt(firstSample + ((static_cast<double>(x) + 0.5) * samplesPerColumn)) * style.displayScale);

    const auto lanes = laneCountFor(data.getNumChannels(), area.getHeight(), style);
    const auto laneHeight = area.getHeight() / static_cast<float>(lanes);
    const auto minHeight = static_cast<float>(columnWidth);
    juce::RectangleList<float> rects;
    for (auto lane = 0; lane < lanes; ++lane)
    {
        const auto top = area.getY() + (laneHeight * static_cast<float>(lane));
        const auto centre = top + (laneHeight / 2.0f);
        const auto half = laneHeight / 2.0f;

        if (!style.centreLineColour.isTransparent())
        {
            g.setColour(style.centreLineColour);
            g.fillRect(visible.getX(), centre - (minHeight / 2.0f), visible.getWidth(), minHeight);
        }

        computeColumns(data, lanes == 1 ? allChannels : lane, firstSample, samplesPerColumn, columns);
        rects.clear();
        rects.ensureStorageAllocated(static_cast<int>(columnCount));
        for (std::size_t x = 0; x < columnCount; ++x)
        {
            const auto& column = columns[x];
            if (!column.valid)
                continue;
            const auto high = std::clamp(column.max * gains[x], -1.0f, 1.0f);
            const auto low = std::clamp(column.min * gains[x], -1.0f, 1.0f);
            const auto y1 = centre - (high * half);
            const auto y2 = centre - (low * half);
            const auto left = area.getX() + static_cast<float>(firstX + (static_cast<double>(x) * columnWidth));
            rects.addWithoutMerging({ left, y1, static_cast<float>(columnWidth), std::max(y2 - y1, minHeight) });
        }
        g.setColour(style.colour);
        g.fillRectList(rects);
    }
}

}
