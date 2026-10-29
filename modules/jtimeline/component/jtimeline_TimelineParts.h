#pragma once

#include "jtimeline_Timeline.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace jtimeline::detail
{

class Ruler final : public juce::Component
{
public:
    explicit Ruler(Timeline& owner);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    void pick(const juce::MouseEvent& event);

    Timeline& timeline; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

class HeaderArea final : public juce::Component
{
public:
    explicit HeaderArea(Timeline& owner);

    void update();
    void clear();
    [[nodiscard]] int getNumPooled() const { return static_cast<int>(pool.size()); }

private:
    Timeline& timeline; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::vector<std::unique_ptr<TrackHeader>> pool;
    std::unordered_map<TrackId, TrackHeader*> bound;
};

class ClipArea final : public juce::Component
{
public:
    explicit ClipArea(Timeline& owner);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    void paintTrack(juce::Graphics& g, TimelineLookAndFeelMethods& methods, int index, juce::Rectangle<float> lane, SampleRange visible);
    void paintOverviewTrack(juce::Graphics& g, TimelineLookAndFeelMethods& methods, int index, juce::Rectangle<float> lane);
    void pickInOverview(const juce::MouseEvent& event);

    Timeline& timeline; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

}
