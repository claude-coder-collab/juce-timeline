#include <jtimeline/core/jtimeline_KeyInput.h>

#include <catch2/catch_test_macros.hpp>

using jtimeline::KeyAction;
using jtimeline::KeyInput;
namespace keys = jtimeline::keys;

TEST_CASE("Plain T and R zoom, in either case", "[keys]")
{
    for (const auto isMac : { true, false })
    {
        CHECK(keys::actionFor({ .character = U'T' }, isMac) == KeyAction::zoomIn);
        CHECK(keys::actionFor({ .character = U'r' }, isMac) == KeyAction::zoomOut);
        CHECK(keys::actionFor({ .character = U't', .shift = true }, isMac) == KeyAction::none);
        CHECK(keys::actionFor({ .character = U't', .command = true }, isMac) == KeyAction::none);
    }
}

TEST_CASE("Option/Alt + A zooms to fit and Command/Ctrl + A selects all", "[keys]")
{
    for (const auto isMac : { true, false })
    {
        CHECK(keys::actionFor({ .character = U'A', .alt = true }, isMac) == KeyAction::zoomToFit);
        CHECK(keys::actionFor({ .character = U'a', .command = true, .control = !isMac }, isMac) == KeyAction::selectAll);
        CHECK(keys::actionFor({ .character = U'a' }, isMac) == KeyAction::none);
        CHECK(keys::actionFor({ .character = U'a', .alt = true, .command = true }, isMac) == KeyAction::none);
    }
    CHECK(keys::actionFor({ .character = U'a', .alt = true, .control = true }, true) == KeyAction::none);
}

TEST_CASE("Brackets zoom with Command and change waveform height with Command + Option", "[keys]")
{
    CHECK(keys::actionFor({ .character = U']', .command = true }, true) == KeyAction::zoomIn);
    CHECK(keys::actionFor({ .character = U'[', .command = true }, true) == KeyAction::zoomOut);
    CHECK(keys::actionFor({ .character = U']', .alt = true, .command = true }, true) == KeyAction::waveformTaller);
    CHECK(keys::actionFor({ .character = U'[', .alt = true, .command = true, .control = true }, false) == KeyAction::waveformShorter);
    CHECK(keys::actionFor({ .character = U']' }, true) == KeyAction::none);
    CHECK(keys::actionFor({ .character = U']', .alt = true }, true) == KeyAction::none);
}

TEST_CASE("Track height uses Control on macOS and Ctrl elsewhere", "[keys]")
{
    CHECK(keys::actionFor({ .named = KeyInput::Named::up, .control = true }, true) == KeyAction::trackTaller);
    CHECK(keys::actionFor({ .named = KeyInput::Named::down, .control = true }, true) == KeyAction::trackShorter);
    CHECK(keys::actionFor({ .named = KeyInput::Named::up, .command = true }, true) == KeyAction::none);
    CHECK(keys::actionFor({ .named = KeyInput::Named::up, .command = true, .control = true }, false) == KeyAction::trackTaller);
    CHECK(keys::actionFor({ .named = KeyInput::Named::down, .command = true, .control = true }, false) == KeyAction::trackShorter);
    CHECK(keys::actionFor({ .named = KeyInput::Named::up }, false) == KeyAction::none);
    CHECK(keys::actionFor({ .named = KeyInput::Named::up, .shift = true, .command = true, .control = true }, false) == KeyAction::none);
}

TEST_CASE("Home and End move the view to the timeline ends", "[keys]")
{
    for (const auto isMac : { true, false })
    {
        CHECK(keys::actionFor({ .named = KeyInput::Named::home }, isMac) == KeyAction::scrollToStart);
        CHECK(keys::actionFor({ .named = KeyInput::Named::end }, isMac) == KeyAction::scrollToEnd);
        CHECK(keys::actionFor({ .named = KeyInput::Named::home, .command = true }, isMac) == KeyAction::none);
        CHECK(keys::actionFor({ .named = KeyInput::Named::end, .shift = true }, isMac) == KeyAction::none);
    }
}
