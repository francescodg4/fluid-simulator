#include "fluid/TransferFunction.hpp"

#include <algorithm>

namespace fluid {
namespace {

    /** Google's Turbo colormap, polynomial approximation (Mikhailov, 2019). */
    Vec3f turbo(float t)
    {
        const float t2 = t * t, t3 = t2 * t, t4 = t3 * t, t5 = t4 * t;
        const float r = 0.13572138f + 4.61539260f * t - 42.66032258f * t2 + 132.13108234f * t3 - 152.94239396f * t4 + 59.28637943f * t5;
        const float g = 0.09140261f + 2.19418839f * t + 4.84296658f * t2 - 14.18503333f * t3 + 4.27729857f * t4 + 2.82956604f * t5;
        const float b = 0.10667330f + 12.64194608f * t - 60.58204836f * t2 + 110.36276771f * t3 - 89.90310912f * t4 + 27.34824973f * t5;
        return { std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f) };
    }

    std::vector<TransferFunction::ColorStop> stopsFor(Colormap colormap)
    {
        switch (colormap) {
        case Colormap::Turbo: {
            std::vector<TransferFunction::ColorStop> stops;
            for (int i = 0; i <= 16; ++i) {
                const float t = static_cast<float>(i) / 16.0f;
                stops.push_back({ t, turbo(t) });
            }
            return stops;
        }
        case Colormap::Rainbow:
            return { { 0.00f, { 0.10f, 0.10f, 0.85f } }, { 0.25f, { 0.00f, 0.70f, 1.00f } }, { 0.50f, { 0.10f, 0.90f, 0.20f } },
                { 0.75f, { 1.00f, 0.90f, 0.00f } }, { 1.00f, { 0.95f, 0.05f, 0.05f } } };
        case Colormap::Viridis:
            return { { 0.00f, { 0.267f, 0.005f, 0.329f } }, { 0.25f, { 0.229f, 0.322f, 0.546f } }, { 0.50f, { 0.128f, 0.567f, 0.551f } },
                { 0.75f, { 0.369f, 0.789f, 0.383f } }, { 1.00f, { 0.993f, 0.906f, 0.144f } } };
        case Colormap::Inferno:
            return { { 0.00f, { 0.001f, 0.000f, 0.014f } }, { 0.25f, { 0.341f, 0.062f, 0.429f } }, { 0.50f, { 0.735f, 0.216f, 0.330f } },
                { 0.75f, { 0.978f, 0.557f, 0.035f } }, { 1.00f, { 0.988f, 0.998f, 0.645f } } };
        case Colormap::CoolWarm:
            return { { 0.00f, { 0.230f, 0.299f, 0.754f } }, { 0.25f, { 0.552f, 0.690f, 0.996f } }, { 0.50f, { 0.866f, 0.866f, 0.866f } },
                { 0.75f, { 0.956f, 0.604f, 0.482f } }, { 1.00f, { 0.706f, 0.016f, 0.150f } } };
        case Colormap::Ice:
            return { { 0.00f, { 0.02f, 0.04f, 0.12f } }, { 0.30f, { 0.08f, 0.22f, 0.50f } }, { 0.60f, { 0.20f, 0.58f, 0.86f } },
                { 0.85f, { 0.60f, 0.88f, 0.98f } }, { 1.00f, { 0.95f, 1.00f, 1.00f } } };
        }
        return {};
    }

} // namespace

TransferFunction::TransferFunction()
{
    setColormap(Colormap::Turbo);
    m_opacity = { { 0.0f, 0.0f }, { 0.35f, 0.02f }, { 0.7f, 0.25f }, { 1.0f, 0.8f } };
}

TransferFunction TransferFunction::fromColormap(Colormap colormap)
{
    TransferFunction tf;
    tf.setColormap(colormap);
    return tf;
}

void TransferFunction::setColormap(Colormap colormap)
{
    m_colormap = colormap;
    m_colors = stopsFor(colormap);
}

void TransferFunction::setOpacityPoints(std::vector<OpacityPoint> points)
{
    for (OpacityPoint& p : points) {
        p.t = std::clamp(p.t, 0.0f, 1.0f);
        p.alpha = std::clamp(p.alpha, 0.0f, 1.0f);
    }
    std::stable_sort(points.begin(), points.end(), [](const OpacityPoint& a, const OpacityPoint& b) { return a.t < b.t; });
    m_opacity = std::move(points);
}

Vec3f TransferFunction::color(float t) const
{
    if (m_colors.empty()) {
        return {};
    }
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= m_colors.front().t) {
        return m_colors.front().rgb;
    }
    if (t >= m_colors.back().t) {
        return m_colors.back().rgb;
    }
    for (std::size_t i = 1; i < m_colors.size(); ++i) {
        if (t <= m_colors[i].t) {
            const ColorStop& a = m_colors[i - 1];
            const ColorStop& b = m_colors[i];
            const float span = b.t - a.t;
            return lerp(a.rgb, b.rgb, span > 0.0f ? (t - a.t) / span : 0.0f);
        }
    }
    return m_colors.back().rgb;
}

float TransferFunction::opacity(float t) const
{
    if (m_opacity.empty()) {
        return 1.0f;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= m_opacity.front().t) {
        return m_opacity.front().alpha;
    }
    if (t >= m_opacity.back().t) {
        return m_opacity.back().alpha;
    }
    for (std::size_t i = 1; i < m_opacity.size(); ++i) {
        if (t <= m_opacity[i].t) {
            const OpacityPoint& a = m_opacity[i - 1];
            const OpacityPoint& b = m_opacity[i];
            const float span = b.t - a.t;
            const float s = span > 0.0f ? (t - a.t) / span : 0.0f;
            return a.alpha + (b.alpha - a.alpha) * s;
        }
    }
    return m_opacity.back().alpha;
}

Rgba TransferFunction::sample(float t) const
{
    const Vec3f c = color(t);
    return { c.x, c.y, c.z, opacity(t) };
}

std::vector<Rgba> TransferFunction::bake(int size) const
{
    std::vector<Rgba> table(static_cast<std::size_t>(std::max(size, 0)));
    for (int i = 0; i < size; ++i) {
        table[static_cast<std::size_t>(i)] = sample(size > 1 ? static_cast<float>(i) / static_cast<float>(size - 1) : 0.0f);
    }
    return table;
}

} // namespace fluid
