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
    const auto snapDown = [pixelScale](double value) { return std::floor((value * pixelScale) + 1.0e-6) / pixelScale; };
    const auto snapUp = [pixelScale](double value) { return std::ceil((value * pixelScale) - 1.0e-6) / pixelScale; };

    const auto firstColumn = std::max(std::floor(static_cast<double>(visible.getX()) * pixelScale), std::ceil((static_cast<double>(area.getX()) * pixelScale) - 1.0e-6));
    const auto endColumn = std::min(std::ceil(static_cast<double>(visible.getRight()) * pixelScale), std::floor((static_cast<double>(area.getRight()) * pixelScale) + 1.0e-6));
    if (!(endColumn > firstColumn))
        return;
    const auto columnCount = static_cast<std::size_t>(endColumn - firstColumn);
    const auto firstX = firstColumn * columnWidth;
    const auto samplesPerPixel = (sourceEnd - sourceStart) / static_cast<double>(area.getWidth());
    const auto samplesPerColumn = samplesPerPixel * columnWidth;
    const auto firstSample = sourceStart + ((firstX - static_cast<double>(area.getX())) * samplesPerPixel);

    std::vector<ColumnPeak> columns(columnCount);
    std::vector<float> gains(columnCount);
    for (std::size_t x = 0; x < columnCount; ++x)
        gains[x] = static_cast<float>(style.envelope.gainAt(firstSample + ((static_cast<double>(x) + 0.5) * samplesPerColumn)) * style.displayScale);

    const auto lanes = laneCountFor(data.getNumChannels(), area.getHeight(), style);
    const auto laneHeight = static_cast<double>(area.getHeight()) / static_cast<double>(lanes);
    juce::RectangleList<float> rects;
    for (auto lane = 0; lane < lanes; ++lane)
    {
        const auto top = static_cast<double>(area.getY()) + (laneHeight * static_cast<double>(lane));
        const auto half = laneHeight / 2.0;
        const auto centre = snapDown(top + half);

        if (!style.centreLineColour.isTransparent())
        {
            g.setColour(style.centreLineColour);
            g.fillRect(juce::Rectangle<double>{ firstX, centre, static_cast<double>(columnCount) * columnWidth, columnWidth }.toFloat());
        }

        computeColumns(data, lanes == 1 ? allChannels : lane, firstSample, samplesPerColumn, columns);
        rects.clear();
        rects.ensureStorageAllocated(static_cast<int>(columnCount));
        for (std::size_t x = 0; x < columnCount; ++x)
        {
            const auto& column = columns[x];
            if (!column.valid)
                continue;
            const auto high = static_cast<double>(std::clamp(column.max * gains[x], -1.0f, 1.0f));
            const auto low = static_cast<double>(std::clamp(column.min * gains[x], -1.0f, 1.0f));
            const auto y1 = snapDown(centre - (high * half));
            const auto y2 = std::max(snapUp(centre - (low * half)), y1 + columnWidth);
            rects.addWithoutMerging(juce::Rectangle<double>{ firstX + (static_cast<double>(x) * columnWidth), y1, columnWidth, y2 - y1 }.toFloat());
        }
        g.setColour(style.colour);
        g.fillRectList(rects);
    }
}

}
