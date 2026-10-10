#include "../src/Math/DisplayMath.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

struct A { int frame; };
static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)
static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

int main() {
    // ---------- lastIndexAtOrBefore ----------
    std::vector<A> acts = { {10}, {20}, {20}, {35}, {100} };
    CHECK(dispmath::lastIndexAtOrBefore(acts, 5)   == -1);
    CHECK(dispmath::lastIndexAtOrBefore(acts, 10)  == 0);
    CHECK(dispmath::lastIndexAtOrBefore(acts, 19)  == 0);
    CHECK(dispmath::lastIndexAtOrBefore(acts, 20)  == 2); // last of the duplicate-frame group
    CHECK(dispmath::lastIndexAtOrBefore(acts, 99)  == 3);
    CHECK(dispmath::lastIndexAtOrBefore(acts, 100) == 4);
    CHECK(dispmath::lastIndexAtOrBefore(acts, 99999) == 4);
    CHECK(dispmath::lastIndexAtOrBefore(std::vector<A>{}, 5) == -1);

    // ---------- basePercentAtIndex: the user's scenario ----------
    // 100 inputs, total base L* = 500. First 5 inputs (5%) already reach 450 (= 90%).
    std::vector<double> v(100);
    for (int i = 0; i < 5; i++)  v[i] = 450.0 * (i + 1) / 5.0;      // 90, 180, 270, 360, 450
    for (int i = 5; i < 100; i++) v[i] = 450.0 + 50.0 * (i - 4) / 95.0; // slow climb to 500
    CHECK(near(v.back(), 500.0));
    CHECK(near(dispmath::basePercentAtIndex(v, 4), 90.0));          // 5% of inputs -> 90% of L*
    CHECK(near(dispmath::basePercentAtIndex(v, 99), 100.0));        // end is exactly 100%
    CHECK(near(dispmath::basePercentAtIndex(v, 0), 18.0));          // 90/500
    // Strictly increases per input, weighted by L* contribution (NOT linear in input count):
    double prev = -1.0;
    for (int i = 0; i < 100; i++) {
        double p = dispmath::basePercentAtIndex(v, i);
        CHECK(p > prev);
        prev = p;
    }
    CHECK(dispmath::basePercentAtIndex(v, 49) > 90.0);              // way above the "linear" 50%

    // ---------- basePercentAtIndex: edge cases ----------
    CHECK(dispmath::basePercentAtIndex({}, 0) == 0.0);              // no data
    CHECK(dispmath::basePercentAtIndex(v, -1) == 0.0);              // before first input
    CHECK(dispmath::basePercentAtIndex(v, 100) == 0.0);             // out of range
    CHECK(dispmath::basePercentAtIndex({ 0.001, 0.001 }, 1) == 100.0);   // MINLEFT floor case, no div-by-zero
    CHECK(dispmath::basePercentAtIndex({ 0.0 }, 0) == 0.0);         // total 0 -> 0, not NaN
    CHECK(dispmath::basePercentAtIndex({ 5.0, 3.0 }, 1) == 100.0);  // single-input-style
    CHECK(near(dispmath::basePercentAtIndex({ 600.0, 500.0 }, 0), 100.0)); // dip in data is clamped to <=100
    double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(dispmath::basePercentAtIndex({ nan, 5.0 }, 0) == 0.0);    // NaN never leaks to the HUD
    CHECK(dispmath::basePercentAtIndex({ 1.0, nan }, 0) == 0.0);
    CHECK(dispmath::basePercentAtIndex({ 1.0, std::numeric_limits<double>::infinity() }, 0) == 0.0);

    // ---------- clampOr ----------
    CHECK(near(dispmath::clampOr(1.5, 0.5, 2.5, 1.0), 1.5));
    CHECK(near(dispmath::clampOr(9.0, 0.5, 2.5, 1.0), 2.5));
    CHECK(near(dispmath::clampOr(-3, 0.5, 2.5, 1.0), 0.5));
    CHECK(near(dispmath::clampOr(nan, 0.5, 2.5, 1.0), 1.0));       // corrupt save -> default

    // ---------- slider mapping round-trips ----------
    for (double lo : {0.5, 0.2, 0.6}) {
        double hi = (lo == 0.2) ? 1.0 : (lo == 0.6 ? 1.4 : 2.5);
        for (double val = lo; val <= hi + 1e-9; val += 0.05) {
            float t = dispmath::valueToSlider(val, lo, hi);
            CHECK(t >= 0.f && t <= 1.f);
            double back = dispmath::sliderToValue(t, lo, hi, 0.05);
            CHECK(near(back, std::round(val / 0.05) * 0.05, 1e-3));
        }
    }
    CHECK(near(dispmath::sliderToValue(0.f, 0.5, 2.5, 0.05), 0.5));
    CHECK(near(dispmath::sliderToValue(1.f, 0.5, 2.5, 0.05), 2.5));
    CHECK(near(dispmath::sliderToValue(-0.3f, 0.5, 2.5, 0.05), 0.5)); // out-of-range thumb values clamp
    CHECK(near(dispmath::sliderToValue(7.f, 0.5, 2.5, 0.05), 2.5));
    CHECK(near(dispmath::sliderToValue(0.3f, 0.5, 2.5, 0.05), 1.1));  // 0.5+0.6 -> exactly 1.1, no 1.0999999
    CHECK(dispmath::valueToSlider(1.0, 1.0, 1.0) == 0.f);             // degenerate range, no div-by-zero
    // default (1.0) sits where the slider thumb is initialised and maps back to exactly 1.0
    CHECK(near(dispmath::sliderToValue(dispmath::valueToSlider(1.0, 0.5, 2.5), 0.5, 2.5, 0.05), 1.0));
    CHECK(near(dispmath::sliderToValue(dispmath::valueToSlider(1.0, 0.2, 1.0), 0.2, 1.0, 0.05), 1.0));

    // ---------- fitPopupScale ----------
    // Default scale must NEVER alter existing layouts, even on a cramped 4:3 screen.
    CHECK(dispmath::fitPopupScale(1.0, 430, 280, 569, 320) == 1.0f);
    CHECK(dispmath::fitPopupScale(1.0, 430, 280, 427, 320) == 1.0f);
    // Shrinking is always honoured.
    CHECK(near(dispmath::fitPopupScale(0.6, 430, 280, 569, 320), 0.6, 1e-6));
    // Enlarging the 430x280 editor on a 569x320 screen is capped so it still fits (308/280 = 1.1).
    CHECK(near(dispmath::fitPopupScale(1.4, 430, 280, 569, 320), 308.0 / 280.0, 1e-5));
    // ...but a small popup can use the full requested scale.
    CHECK(near(dispmath::fitPopupScale(1.4, 220, 130, 569, 320), 1.4, 1e-6));
    // Result never leaves the screen when enlarged:
    for (double req = 1.0; req <= 1.4; req += 0.05) {
        float s = dispmath::fitPopupScale(req, 430, 280, 569, 320);
        CHECK(430 * s <= 569 - 12 + 1e-3 || s == 1.0f);
        CHECK(280 * s <= 320 - 12 + 1e-3 || s == 1.0f);
    }
    // Garbage in -> sane out.
    CHECK(dispmath::fitPopupScale(nan, 430, 280, 569, 320) == 1.0f);
    CHECK(dispmath::fitPopupScale(1.4, 0, 0, 569, 320) == 1.4f);    // unknown popup size: no clamp, no crash
    CHECK(dispmath::fitPopupScale(-5.0, 430, 280, 569, 320) >= 0.1f);

    if (fails == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d FAILURES\n", fails);
    return fails ? 1 : 0;
}
