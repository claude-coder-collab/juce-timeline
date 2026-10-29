#pragma once

namespace jtimeline
{

//==============================================================================
/** A key press, in Pro Tools terms. */
struct KeyInput
{
    /** A key that has no character. */
    enum class Named {
        none, /**< The key is identified by its character. */
        up, /**< Up arrow. */
        down, /**< Down arrow. */
        home, /**< Home. */
        end /**< End. */
    };

    /**
        The character the key produces without modifiers (JUCE's key code), so that bindings work
        on any keyboard layout and with Option held, which changes the typed character on macOS.
        Letters may be either case.
    */
    char32_t character = 0;
    Named named = Named::none; /**< Set for keys with no character. */
    bool shift = false; /**< Shift. */
    bool alt = false; /**< Option on macOS, Alt elsewhere. */
    bool command = false; /**< Command on macOS, Ctrl elsewhere. */
    bool control = false; /**< Control on macOS; elsewhere the same as command. */
};

/** What a key press does to a timeline view. */
enum class KeyAction {
    none, /**< Not bound. */
    zoomIn, /**< Horizontal zoom in, anchored at the edit cursor. */
    zoomOut, /**< Horizontal zoom out, anchored at the edit cursor. */
    zoomToFit, /**< Zoom out to show the whole timeline. */
    waveformTaller, /**< Waveform height up. */
    waveformShorter, /**< Waveform height down. */
    trackTaller, /**< Track height up. */
    trackShorter, /**< Track height down. */
    selectAll, /**< Select all clips. */
    scrollToStart, /**< Move the view to the timeline start. */
    scrollToEnd /**< Move the view to the timeline end. */
};

/** Keyboard bindings, following Pro Tools' defaults. */
namespace keys
{

/**
    Returns the action for a key press. Bindings (macOS / Windows and Linux):
    T and R zoom in and out; Command/Ctrl + ] and [ zoom in and out; Option/Alt + A zooms to fit;
    Command + Option / Ctrl + Alt + ] and [ change waveform height; Control + Up and Down on macOS,
    Ctrl + Up and Down elsewhere, change track height; Command/Ctrl + A selects all clips.
    Any other modifier combination does nothing.

    @param isMac  true on macOS, where Control is separate from Command
*/
[[nodiscard]] KeyAction actionFor(const KeyInput& key, bool isMac);

}

}
