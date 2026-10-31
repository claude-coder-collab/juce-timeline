#include <jtimeline/core/jtimeline_WheelInput.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using jtimeline::WheelAction;
using jtimeline::WheelDelta;
using jtimeline::WheelModifiers;
namespace wheel = jtimeline::wheel;

namespace
{

constexpr WheelDelta notchUp{ .deltaX = 0.0, .deltaY = 0.234, .isSmooth = false };
constexpr WheelDelta notchDown{ .deltaX = 0.0, .deltaY = -0.039, .isSmooth = false };

}

TEST_CASE("Wheel alone scrolls tracks", "[wheel]")
{
    const auto action = wheel::actionFor({}, notchDown);
    CHECK(action == WheelAction{ .scrollTracksPixels = wheel::pixelsPerNotch });
    CHECK(wheel::actionFor({}, notchUp).scrollTracksPixels == -wheel::pixelsPerNotch);
}

TEST_CASE("Trackpad horizontal delta scrolls time without modifiers", "[wheel]")
{
    const auto action = wheel::actionFor({}, { .deltaX = -0.1, .deltaY = 0.05, .isSmooth = true });
    CHECK(action.scrollTimePixels == Approx(0.1 * wheel::pixelsPerSmoothDelta));
    CHECK(action.scrollTracksPixels == Approx(-0.05 * wheel::pixelsPerSmoothDelta));
}

TEST_CASE("Shift + wheel scrolls time with either delta", "[wheel]")
{
    const WheelModifiers shift{ .shift = true };
    CHECK(wheel::actionFor(shift, notchDown) == WheelAction{ .scrollTimePixels = wheel::pixelsPerNotch });
    const WheelDelta macShifted{ .deltaX = 0.039, .deltaY = 0.0, .isSmooth = false };
    CHECK(wheel::actionFor(shift, macShifted) == WheelAction{ .scrollTimePixels = -wheel::pixelsPerNotch });
}

TEST_CASE("Alt + wheel zooms at the edit cursor, one step per notch", "[wheel]")
{
    CHECK(wheel::actionFor({ .alt = true }, notchUp) == WheelAction{ .zoomAtAnchorSteps = 1.0 });
    CHECK(wheel::actionFor({ .alt = true }, notchDown) == WheelAction{ .zoomAtAnchorSteps = -1.0 });
}

TEST_CASE("Command + wheel zooms at the pointer", "[wheel]")
{
    CHECK(wheel::actionFor({ .command = true }, notchUp) == WheelAction{ .zoomAtPointerSteps = 1.0 });
    const auto smooth = wheel::actionFor({ .command = true }, { .deltaX = 0.0, .deltaY = -0.25, .isSmooth = true });
    CHECK(smooth.zoomAtPointerSteps == Approx(-2.0));
}

TEST_CASE("Alt + Shift + wheel up makes waveforms smaller, with either delta", "[wheel]")
{
    const WheelModifiers altShift{ .shift = true, .alt = true };
    CHECK(wheel::actionFor(altShift, notchUp) == WheelAction{ .waveformZoomSteps = -1.0 });
    CHECK(wheel::actionFor(altShift, { .deltaX = -0.039, .deltaY = 0.0, .isSmooth = false }) == WheelAction{ .waveformZoomSteps = 1.0 });
}

TEST_CASE("Unbound modifier combinations do nothing", "[wheel]")
{
    CHECK(wheel::actionFor({ .shift = true, .command = true }, notchUp) == WheelAction{});
    CHECK(wheel::actionFor({ .alt = true, .command = true }, notchUp) == WheelAction{});
    CHECK(wheel::actionFor({ .other = true }, notchUp) == WheelAction{});
    CHECK(wheel::actionFor({ .alt = true }, {}) == WheelAction{});
}

TEST_CASE("Zoom steps follow the specified factors", "[wheel]")
{
    CHECK(wheel::zoomFactorPerNotch == Approx(std::sqrt(2.0)));
    CHECK(wheel::waveformFactorPerStep == Approx(std::pow(2.0, 0.25)));
    CHECK(wheel::zoomedSamplesPerPixel(100.0, 2.0, wheel::zoomFactorPerNotch) == Approx(50.0));
    CHECK(wheel::zoomedSamplesPerPixel(100.0, -1.0, wheel::zoomFactorPerKey) == Approx(200.0));
}
