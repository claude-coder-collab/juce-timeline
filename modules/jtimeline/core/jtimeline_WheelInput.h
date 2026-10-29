#pragma once

#include <numbers>

namespace jtimeline
{

//==============================================================================
/** The modifier keys held during a wheel event, in Pro Tools terms. */
struct WheelModifiers
{
    bool shift = false; /**< Shift. */
    bool alt = false; /**< Option on macOS, Alt elsewhere. */
    bool command = false; /**< Command on macOS, Ctrl elsewhere. */
    bool other = false; /**< Any other modifier, such as Control on macOS; disables every binding. */

    /** Compares two sets of modifiers. */
    friend bool operator==(const WheelModifiers&, const WheelModifiers&) = default;
};

/** A wheel event's deltas, as JUCE reports them. */
struct WheelDelta
{
    double deltaX = 0.0; /**< Horizontal delta; positive means towards the left. */
    double deltaY = 0.0; /**< Vertical delta; positive means upwards. */
    bool isSmooth = false; /**< True for trackpads and other precise devices, false for notched wheels. */
};

/** The effect of a wheel event on a timeline view, following Pro Tools' default bindings. */
struct WheelAction
{
    /** Number of pixels to move the view down; negative moves it up. */
    double scrollTracksPixels = 0.0;
    /** Number of pixels to move the view later in time; negative moves it earlier. */
    double scrollTimePixels = 0.0;
    /** Number of horizontal zoom-in steps anchored at the edit cursor; negative zooms out. */
    double zoomAtEditCursorSteps = 0.0;
    /** Number of horizontal zoom-in steps anchored at the pointer; negative zooms out. */
    double zoomAtPointerSteps = 0.0;
    /** Number of waveform height steps up; negative goes down. */
    double waveformZoomSteps = 0.0;

    /** Compares two actions. */
    friend bool operator==(const WheelAction&, const WheelAction&) = default;
};

/** Constants and helpers for interpreting wheel input. */
namespace wheel
{

/** The smooth (trackpad) delta treated as one wheel notch: 64 points of scrolling on macOS. */
inline constexpr double smoothDeltaPerNotch = 0.125;
/** The pixels scrolled by one notch of a notched wheel. */
inline constexpr double pixelsPerNotch = 60.0;
/** The pixels scrolled by a smooth delta of 1, so that trackpad scrolling follows the fingers. */
inline constexpr double pixelsPerSmoothDelta = 512.0;
/** The horizontal zoom factor of one wheel step. */
inline constexpr double zoomFactorPerNotch = std::numbers::sqrt2;
/** The horizontal zoom factor of one key press. */
inline constexpr double zoomFactorPerKey = 2.0;
/** The waveform height factor of one wheel or key step. */
inline constexpr double waveformFactorPerStep = 1.189207115002721;

/**
    Returns the action for a wheel event. A notched wheel counts one step per event, whatever
    size the platform reports; a smooth delta counts one step per smoothDeltaPerNotch.

    Bindings: wheel scrolls tracks, with a trackpad's horizontal delta scrolling time directly;
    Shift scrolls time; Alt zooms at the edit cursor; Command zooms at the pointer; Alt + Shift
    changes waveform height. Bindings with Shift use whichever delta is non-zero, since macOS
    turns Shift + wheel into a horizontal delta. Other combinations do nothing.
*/
[[nodiscard]] WheelAction actionFor(WheelModifiers modifiers, WheelDelta delta);

/** Returns the samples-per-pixel after zooming in by a number of steps of the given factor. */
[[nodiscard]] double zoomedSamplesPerPixel(double samplesPerPixel, double steps, double factorPerStep);

}

}
