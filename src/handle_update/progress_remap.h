#pragma once

namespace fs {

/**
 * Remap a downstream installer's progress emission `p` (in [0..100]) onto
 * the band [extract_pct..100] reserved for the post-extract dispatch phase.
 *
 * Used by FSUpdate::update_image() on the v2.0 path so the bar moves through
 * the extract loop (0..extract_pct) and then through the dispatch phase
 * (extract_pct..100) without resetting in the middle. Pure arithmetic;
 * `p` and `extract_pct` are clamped to [0..100] before the remap.
 *
 * Examples for extract_pct = 20:
 *   remap_extract_progress(  0, 20) -> 20    (dispatch start)
 *   remap_extract_progress( 50, 20) -> 60    (dispatch midpoint)
 *   remap_extract_progress(100, 20) -> 100   (dispatch done)
 */
[[nodiscard]] constexpr int remap_extract_progress(int p, int extract_pct) noexcept
{
    if (p < 0)             p = 0;
    if (p > 100)           p = 100;
    if (extract_pct < 0)   extract_pct = 0;
    if (extract_pct > 100) extract_pct = 100;
    return extract_pct + (p * (100 - extract_pct)) / 100;
}

} // namespace fs
