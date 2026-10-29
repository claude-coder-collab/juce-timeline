#pragma once

namespace jtimeline
{

/** How much of a clip is drawn, chosen by its width on screen. */
enum class ClipTier {
    full, /**< Translucent rounded body, outline, name, waveform and fades. */
    reduced, /**< Rounded body and outline, waveform; no name or fades. */
    minimal, /**< An opaque rectangle in the clip colour. */
    coverage /**< Not drawn alone; part of the track's coverage strip. */
};

/** The least on-screen width, in pixels, for each tier. */
struct DetailThresholds
{
    double full = 40.0; /**< The least width drawn in full. */
    double reduced = 4.0; /**< The least width drawn reduced. */
    double minimal = 1.0; /**< The least width drawn on its own. */
};

/** Returns the tier for a clip's on-screen width. */
[[nodiscard]] constexpr ClipTier tierFor(double widthPixels, const DetailThresholds& thresholds)
{
    if (widthPixels >= thresholds.full)
        return ClipTier::full;
    if (widthPixels >= thresholds.reduced)
        return ClipTier::reduced;
    if (widthPixels >= thresholds.minimal)
        return ClipTier::minimal;
    return ClipTier::coverage;
}

}
