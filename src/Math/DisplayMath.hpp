#pragma once
// Pure helpers with NO Geode / cocos dependency, so they can be unit-tested outside the game.
#include <algorithm>
#include <cmath>
#include <vector>

namespace dispmath {

// Index of the last action whose `.frame` is <= `frame`, or -1 if there is none.
// `actions` must be sorted ascending by `.frame` (g_validActions always is).
template <class Actions>
inline int lastIndexAtOrBefore(Actions const& actions, int frame) {
    int lo = 0;
    int hi = static_cast<int>(actions.size());
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (actions[mid].frame > frame) hi = mid;
        else lo = mid + 1;
    }
    return lo - 1;
}

// "Precision percentage" for BASE L* only.
//
// vBase[i] is the base L* of the level after input i (it grows as inputs are added), and
// vBase.back() is the L* of the whole level. The percentage is how much of that final L*
// has been reached at input `idx`:   vBase[idx] / vBase.back() * 100.
//
// It is therefore weighted by L* contribution per input, NOT by time / frame / input count:
// a level whose first 5% of inputs already carries 90% of the total L* shows 90% there.
inline double basePercentAtIndex(std::vector<double> const& vBase, int idx) {
    if (vBase.empty() || idx < 0 || idx >= static_cast<int>(vBase.size())) return 0.0;
    double total = vBase.back();
    double v = vBase[static_cast<size_t>(idx)];
    if (!std::isfinite(total) || !std::isfinite(v) || total <= 0.0 || v <= 0.0) return 0.0;
    return std::clamp(v / total * 100.0, 0.0, 100.0);
}

// Clamp, falling back to `fallback` for NaN / inf (corrupt save data).
inline double clampOr(double v, double lo, double hi, double fallback) {
    if (!std::isfinite(v)) return fallback;
    return std::clamp(v, lo, hi);
}

// Map a real value in [lo, hi] to a GD slider position in [0, 1].
inline float valueToSlider(double v, double lo, double hi) {
    if (!(hi > lo)) return 0.f;
    return static_cast<float>(std::clamp((v - lo) / (hi - lo), 0.0, 1.0));
}

// Map a GD slider position in [0, 1] back to a real value in [lo, hi], snapped to `step`.
inline double sliderToValue(float t, double lo, double hi, double step) {
    double tt = std::clamp(static_cast<double>(t), 0.0, 1.0);
    double v = lo + tt * (hi - lo);
    if (step > 0.0) v = std::round(v / step) * step;
    v = std::round(v * 1000.0) / 1000.0; // strip float noise (0.30000001 -> 0.3)
    return std::clamp(v, lo, hi);
}

// Final scale for a popup of size popupW x popupH on a winW x winH screen.
// - Never changes anything at the default scale (<= 1.0 is returned as-is), so existing
//   layouts are untouched unless the user actually moves the slider.
// - When enlarging (> 1.0) the popup is capped so it can never be pushed off-screen.
inline float fitPopupScale(double requested, float popupW, float popupH,
                           float winW, float winH, float margin = 6.f) {
    double s = std::isfinite(requested) ? requested : 1.0;
    if (s > 1.0 && popupW > 0.f && popupH > 0.f && winW > 0.f && winH > 0.f) {
        double fit = std::min((winW - 2.0 * margin) / popupW, (winH - 2.0 * margin) / popupH);
        s = std::max(1.0, std::min(s, fit));
    }
    return static_cast<float>(std::max(s, 0.1));
}

} // namespace dispmath
