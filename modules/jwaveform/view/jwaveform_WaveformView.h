#pragma once

#include "../core/jwaveform_PeakCache.h"
#include "jwaveform_WaveformDrawing.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace jwaveform
{

//==============================================================================
/**
    A component showing a span of one source's waveform, built through a PeakCache and redrawn as
    the build progresses. It shows a message while loading with no peaks yet, or when the source
    can't be read.

    Colours resolve through the component, its parents and the look and feel, falling back to
    standard JUCE look-and-feel colours. The host application chooses the colour scheme.
*/
class WaveformView : public juce::Component
{
public:
    /** Colour ids for setColour(). */
    enum ColourIds // NOLINT(cppcoreguidelines-use-enum-class)
    {
        backgroundColourId = 0x2a51000, /**< The background; transparent by default. */
        waveformColourId = 0x2a51001, /**< The waveform. */
        centreLineColourId = 0x2a51002, /**< The line through each lane's centre. */
        textColourId = 0x2a51003 /**< Loading and failure messages. */
    };

    /** Creates an empty view. */
    WaveformView();
    /** Destructor. */
    ~WaveformView() override;

    WaveformView(const WaveformView&) = delete;
    WaveformView& operator=(const WaveformView&) = delete;
    WaveformView(WaveformView&&) = delete;
    WaveformView& operator=(WaveformView&&) = delete;

    /** Sets the cache that builds peaks; without one, only ready-made peaks are drawn. */
    void setPeakCache(std::shared_ptr<PeakCache> cache);
    /** Sets the source to show, requesting its peaks from the cache. */
    void setSource(const WaveformSource& source);
    /** Shows nothing. */
    void clearSource();

    /** Sets the source span drawn across the view's width, in source samples; an empty span shows the whole source. */
    void setVisibleRange(double sourceStart, double sourceEnd);
    /** Returns the start of the drawn span. */
    [[nodiscard]] double getVisibleStart() const { return visibleStart; }
    /** Returns the end of the drawn span. */
    [[nodiscard]] double getVisibleEnd() const { return visibleEnd; }

    /** Sets the drawing style; its colours are ignored in favour of the colour ids. */
    void setStyle(const WaveformStyle& style);
    /** Returns the drawing style. */
    [[nodiscard]] const WaveformStyle& getStyle() const { return style; }

    /** Returns the state of the current source's peaks. */
    [[nodiscard]] PeakState getState() const { return handle.getState(); }
    /** Returns the current source's peaks, possibly partial, or nullptr. */
    [[nodiscard]] std::shared_ptr<const PeakData> getData() const { return handle.getData(); }

    /** Returns a colour as the view paints it. */
    [[nodiscard]] juce::Colour resolveColour(int colourId) const;

    /** @internal */
    void paint(juce::Graphics& g) override;

private:
    void request();

    std::shared_ptr<PeakCache> cache;
    WaveformSource source;
    PeakHandle handle;
    WaveformStyle style;
    double visibleStart = 0.0;
    double visibleEnd = 0.0;
};

}
