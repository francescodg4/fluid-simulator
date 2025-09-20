#pragma once

#include "fluid/Math.hpp"

#include <array>
#include <string_view>
#include <vector>

namespace fluid {

struct Rgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

enum class Colormap {
    Turbo,
    Rainbow,
    Viridis,
    Inferno,
    CoolWarm,
    Ice,
};

/**
 * Maps a normalised scalar t in [0, 1] to colour (piecewise-linear colour stops) and opacity
 * (piecewise-linear control points), as used by volume renderers and colour legends.
 */
class TransferFunction {
public:
    struct ColorStop {
        float t;
        Vec3f rgb;
    };
    struct OpacityPoint {
        float t;
        float alpha;
    };

    TransferFunction();
    static TransferFunction fromColormap(Colormap colormap);
    static constexpr std::array<std::string_view, 6> colormapNames { "Turbo", "Rainbow", "Viridis", "Inferno", "Cool-Warm", "Ice" };

    void setColormap(Colormap colormap);
    Colormap colormap() const { return m_colormap; }

    const std::vector<ColorStop>& colorStops() const { return m_colors; }
    const std::vector<OpacityPoint>& opacityPoints() const { return m_opacity; }
    /** Replaces the opacity curve; points are sorted and clamped to [0, 1]. */
    void setOpacityPoints(std::vector<OpacityPoint> points);

    Vec3f color(float t) const;
    float opacity(float t) const;
    Rgba sample(float t) const;
    /** Samples the function at @p size evenly spaced positions (lookup table / texture). */
    std::vector<Rgba> bake(int size) const;

private:
    Colormap m_colormap = Colormap::Turbo;
    std::vector<ColorStop> m_colors;
    std::vector<OpacityPoint> m_opacity;
};

} // namespace fluid
