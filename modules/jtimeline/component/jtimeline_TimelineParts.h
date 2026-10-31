#pragma once

#include "jtimeline_Timeline.h"

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jtimeline::detail
{

class SelectedValue final : public juce::AccessibilityTextValueInterface
{
public:
    explicit SelectedValue(std::function<bool()> isSelectedIn) :
        isSelected(std::move(isSelectedIn))
    {
    }

    [[nodiscard]] bool isReadOnly() const override { return true; }
    [[nodiscard]] juce::String getCurrentValueAsString() const override { return isSelected() ? "selected" : ""; }
    void setValueAsString(const juce::String& /*newValue*/) override {}

private:
    std::function<bool()> isSelected;
};

class SelectableHandler final : public juce::AccessibilityHandler
{
public:
    SelectableHandler(juce::Component& component, juce::AccessibilityRole role, const std::function<bool()>& isSelectedIn, const std::function<void(SelectMode)>& select, const std::function<void()>& focused) :
        juce::AccessibilityHandler(component, role, juce::AccessibilityActions{}.addAction(juce::AccessibilityActionType::press, [select] { select(SelectMode::replace); }).addAction(juce::AccessibilityActionType::toggle, [select] { select(SelectMode::toggle); }).addAction(juce::AccessibilityActionType::focus, focused), Interfaces{ std::make_unique<SelectedValue>(isSelectedIn) }),
        isSelected(isSelectedIn)
    {
    }

    [[nodiscard]] juce::AccessibleState getCurrentState() const override
    {
        const auto state = juce::AccessibilityHandler::getCurrentState().withSelectable();
        return isSelected() ? state.withSelected() : state;
    }

private:
    std::function<bool()> isSelected;
};

class Ruler final : public juce::Component
{
public:
    explicit Ruler(Timeline& owner);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    [[nodiscard]] SamplePosition positionAt(float x) const;
    [[nodiscard]] std::optional<SamplePosition> markerAt(juce::Point<float> position);
    [[nodiscard]] juce::Rectangle<float> timeScaleArea() const;

    Timeline& timeline; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::optional<SamplePosition> dragAnchor;
    bool selecting = false;
};

class HeaderArea final : public juce::Component
{
public:
    explicit HeaderArea(Timeline& owner);

    void update();
    void clear();
    void repaintHeaders();
    [[nodiscard]] TrackHeader* headerFor(TrackId track) const;
    [[nodiscard]] int getNumPooled() const { return static_cast<int>(pool.size()); }

private:
    Timeline& timeline; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::vector<std::unique_ptr<TrackHeader>> pool;
    std::unordered_map<TrackId, TrackHeader*> bound;
};

class ClipProxy final : public juce::Component
{
public:
    explicit ClipProxy(Timeline& owner);

    void bind(ClipId id, const ClipInfo& info);
    [[nodiscard]] ClipId getClipId() const { return clip; }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    Timeline& timeline; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    ClipId clip{};
};

class LaneProxy final : public juce::Component
{
public:
    LaneProxy();

    void bind(TrackId id, const juce::String& trackName);
    [[nodiscard]] TrackId getTrackId() const { return track; }
    ClipProxy& proxyAt(Timeline& timeline, std::size_t index);
    void hideFrom(std::size_t index);
    void rowsUpdated();
    [[nodiscard]] std::vector<ClipProxy*> getVisibleProxies() const;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    TrackId track{};
    std::vector<std::unique_ptr<ClipProxy>> proxies;
    std::vector<ClipProxy*> rows;
    std::vector<ClipId> rowIds;
};

class TimelineTraverser final : public juce::ComponentTraverser
{
public:
    explicit TimelineTraverser(Timeline& owner);

    juce::Component* getDefaultComponent(juce::Component* parent) override;
    juce::Component* getNextComponent(juce::Component* current) override;
    juce::Component* getPreviousComponent(juce::Component* current) override;
    std::vector<juce::Component*> getAllComponents(juce::Component* parent) override;

private:
    Timeline& timeline; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

class ClipArea final : public juce::Component, private juce::AsyncUpdater
{
public:
    static constexpr int maxProxies = 1000;

    explicit ClipArea(Timeline& owner);
    ~ClipArea() override;

    ClipArea(const ClipArea&) = delete;
    ClipArea& operator=(const ClipArea&) = delete;
    ClipArea(ClipArea&&) = delete;
    ClipArea& operator=(ClipArea&&) = delete;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    void proxiesNeedUpdate();
    void updateProxies();
    [[nodiscard]] std::vector<ClipProxy*> getVisibleProxies() const;
    [[nodiscard]] LaneProxy* laneFor(TrackId track) const;
    [[nodiscard]] ClipProxy* proxyFor(ClipId clip) const;

private:
    void handleAsyncUpdate() override;
    void paintContents(juce::Graphics& g, TimelineLookAndFeelMethods& methods);
    void paintTrack(juce::Graphics& g, TimelineLookAndFeelMethods& methods, int index, juce::Rectangle<float> lane, SampleRange visible);
    void paintOverviewTrack(juce::Graphics& g, TimelineLookAndFeelMethods& methods, int index, juce::Rectangle<float> lane);
    void pickInOverview(const juce::MouseEvent& event);
    void updateBand(juce::Rectangle<float> next);

    Timeline& timeline; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::vector<std::unique_ptr<LaneProxy>> lanes;
    std::optional<juce::Point<float>> bandStart;
    bool banding = false;
    juce::Rectangle<float> band;
    std::vector<ClipId> bandBase;
    std::optional<ClipId> shiftClicked;
};

}
