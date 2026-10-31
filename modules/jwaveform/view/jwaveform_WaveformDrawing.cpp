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

namespace
{

struct SampleLine
{
    juce::Rectangle<float> area;
    double sourceStart = 0.0;
    double pixelsPerSample = 1.0;
    double centre = 0.0;
    double half = 0.0;
    SampleRange positions;
};

void drawSampleLine(const juce::Graphics& g, const RawSamples& samples, int channel, const SampleLine& line, const WaveformStyle& style)
{
    const auto dots = line.pixelsPerSample >= static_cast<double>(style.sampleDotSpacing);
    const auto radius = style.sampleDotDiameter / 2.0f;
    juce::Path path;
    juce::Path dotPath;
    auto open = false;
    for (auto position = line.positions.start; position < line.positions.end; ++position)
    {
        if (!samples.contains(position))
        {
            open = false;
            continue;
        }
        const auto centreSample = static_cast<double>(position) + 0.5;
        const auto gain = style.envelope.gainAt(centreSample) * style.displayScale;
        const auto value = std::clamp(static_cast<double>(samples.get(channel, position)) * gain, -1.0, 1.0);
        const auto x = static_cast<float>(static_cast<double>(line.area.getX()) + ((centreSample - line.sourceStart) * line.pixelsPerSample));
        const auto y = static_cast<float>(line.centre - (value * line.half));
        if (open)
            path.lineTo(x, y);
        else
            path.startNewSubPath(x, y);
        open = true;
        if (dots)
            dotPath.addEllipse(x - radius, y - radius, style.sampleDotDiameter, style.sampleDotDiameter);
    }
    g.strokePath(path, juce::PathStrokeType{ style.lineThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    if (dots)
        g.fillPath(dotPath);
}

}

void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area, const PeakData& data, double sourceStart, double sourceEnd, const WaveformStyle& style, const RawSamples& samples)
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

    const auto raw = !samples.isEmpty() && usesRawSamples(data, samplesPerColumn);
    const auto asLine = raw && samplesPerColumn < 1.0;
    const auto lastSample = firstSample + (static_cast<double>(columnCount) * samplesPerColumn);
    const auto linePositions = SampleRange{ .start = static_cast<SamplePosition>(std::floor(firstSample)) - 1, .end = static_cast<SamplePosition>(std::ceil(lastSample)) + 1 }.intersection({ .start = 0, .end = data.getLength() });
    const juce::Graphics::ScopedSaveState saved{ g };
    if (asLine)
        g.reduceClipRegion(area.getSmallestIntegerContainer());

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

        const auto channel = lanes == 1 ? allChannels : lane;
        computeColumns(data, channel, firstSample, samplesPerColumn, columns);
        if (asLine)
        {
            for (std::size_t x = 0; x < columnCount; ++x)
            {
                const auto from = firstSample + (static_cast<double>(x) * samplesPerColumn);
                const auto start = static_cast<SamplePosition>(std::floor(from));
                if (samples.contains(SampleRange{ .start = start, .end = std::max(start + 1, static_cast<SamplePosition>(std::ceil(from + samplesPerColumn))) }))
                    columns[x].valid = false;
            }
        }
        else if (raw)
        {
            computeRawColumns(samples, channel, data.getLength(), firstSample, samplesPerColumn, columns);
        }
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

        if (asLine)
        {
            const SampleLine line{ .area = area, .sourceStart = sourceStart, .pixelsPerSample = 1.0 / samplesPerPixel, .centre = centre, .half = half, .positions = linePositions };
            const auto firstChannel = channel == allChannels ? 0 : channel;
            const auto endChannel = channel == allChannels ? data.getNumChannels() : channel + 1;
            for (auto c = firstChannel; c < endChannel; ++c)
                drawSampleLine(g, samples, c, line, style);
        }
    }
}

}
