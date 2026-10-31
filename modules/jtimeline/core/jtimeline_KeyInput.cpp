#include "jtimeline_KeyInput.h"

namespace jtimeline::keys
{

namespace
{

char32_t lower(char32_t character)
{
    return character >= U'A' && character <= U'Z' ? character - U'A' + U'a' : character;
}

}

KeyAction actionFor(const KeyInput& key, bool isMac)
{
    const auto macControl = isMac && key.control;
    const auto plain = !key.shift && !key.alt && !key.command && !macControl;
    const auto commandOnly = !key.shift && !key.alt && key.command && !macControl;
    const auto altOnly = !key.shift && key.alt && !key.command && !macControl;
    const auto commandAlt = !key.shift && key.alt && key.command && !macControl;
    const auto trackHeightModifiers = !key.shift && !key.alt && (isMac ? key.control && !key.command : key.command);

    if (key.named == KeyInput::Named::home || key.named == KeyInput::Named::end)
    {
        if (!plain)
            return KeyAction::none;
        return key.named == KeyInput::Named::home ? KeyAction::scrollToStart : KeyAction::scrollToEnd;
    }
    const auto shiftOnly = key.shift && !key.alt && !key.command && !macControl;
    switch (key.named)
    {
        case KeyInput::Named::up:
        case KeyInput::Named::down:
        {
            const auto up = key.named == KeyInput::Named::up;
            if (trackHeightModifiers)
                return up ? KeyAction::trackTaller : KeyAction::trackShorter;
            if (plain)
                return up ? KeyAction::cursorUp : KeyAction::cursorDown;
            if (shiftOnly)
                return up ? KeyAction::extendUp : KeyAction::extendDown;
            return KeyAction::none;
        }
        case KeyInput::Named::left:
        case KeyInput::Named::right:
        {
            const auto left = key.named == KeyInput::Named::left;
            if (plain)
                return left ? KeyAction::cursorLeft : KeyAction::cursorRight;
            if (shiftOnly)
                return left ? KeyAction::extendLeft : KeyAction::extendRight;
            return KeyAction::none;
        }
        case KeyInput::Named::enter:
            return plain ? KeyAction::cursorEnter : KeyAction::none;
        case KeyInput::Named::escape:
            return plain ? KeyAction::cursorEscape : KeyAction::none;
        case KeyInput::Named::home:
        case KeyInput::Named::end:
        case KeyInput::Named::none:
            break;
    }

    switch (lower(key.character))
    {
        case U't':
            return plain ? KeyAction::zoomIn : KeyAction::none;
        case U'r':
            return plain ? KeyAction::zoomOut : KeyAction::none;
        case U'a':
            if (altOnly)
                return KeyAction::zoomToFit;
            return commandOnly ? KeyAction::selectAll : KeyAction::none;
        case U']':
            if (commandOnly)
                return KeyAction::zoomIn;
            return commandAlt ? KeyAction::waveformTaller : KeyAction::none;
        case U' ':
            return plain ? KeyAction::playStop : KeyAction::none;
        case U'f':
            return altOnly ? KeyAction::zoomToSelection : KeyAction::none;
        case U'[':
            if (commandOnly)
                return KeyAction::zoomOut;
            return commandAlt ? KeyAction::waveformShorter : KeyAction::none;
        default:
            return KeyAction::none;
    }
}

}
