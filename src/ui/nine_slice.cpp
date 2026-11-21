#include "orosmo/ui/nine_slice.hpp"

#include <algorithm>
#include <cmath>

namespace orosmo::ui
{

namespace
{
struct Writer
{
    std::vector<float> &out;
    float texW;
    float texH;

    // Quadrilatère destination [x0, x1] × [y0, y1] ← source [s0, s1] × [t0, t1] (texels).
    void quad(float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1) const
    {
        if (x1 <= x0 || y1 <= y0 || s1 <= s0 || t1 <= t0)
            return;
        const float ix = std::min(0.5f, (s1 - s0) * 0.5f);
        const float iy = std::min(0.5f, (t1 - t0) * 0.5f);
        const float u0 = (s0 + ix) / texW;
        const float u1 = (s1 - ix) / texW;
        const float v0 = (t0 + iy) / texH;
        const float v1 = (t1 - iy) / texH;
        const float v[] = {x0, y0, u0, v0, x0, y1, u0, v1, x1, y1, u1, v1, x0, y0, u0, v0, x1, y1, u1, v1, x1, y0, u1, v0};
        out.insert(out.end(), std::begin(v), std::end(v));
    }
};

// Bande à répéter sur un axe : renvoie les morceaux (début, fin en destination ; fin en source) le long de [d0, d1].
template <typename Fn> void tile(SliceFill fill, float d0, float d1, float s0, float s1, float scale, Fn &&emit)
{
    if (fill == SliceFill::kNone || d1 <= d0 || s1 <= s0)
        return;
    if (fill == SliceFill::kStretch)
    {
        emit(d0, d1, s0, s1);
        return;
    }
    // Nombre de répétitions borné et calculé d'avance (positions d0 + k * période, sans accumulation) : une période minuscule
    // ne fait ni boucler sans fin ni exploser la liste de dessin ; au-delà du budget, la bande est étirée.
    constexpr double kMaxRepeats = 4096.0;
    const float period = (s1 - s0) * scale;
    const double count = period > 0.0f ? std::ceil((static_cast<double>(d1) - d0 - 1e-4) / period) : kMaxRepeats + 1.0;
    if (!(count <= kMaxRepeats))
    {
        emit(d0, d1, s0, s1);
        return;
    }
    for (int k = 0; k < static_cast<int>(count); ++k)
    {
        const float d = d0 + static_cast<float>(k) * period;
        const float end = std::min(d0 + static_cast<float>(k + 1) * period, d1);
        emit(d, end, s0, s0 + (end - d) / scale); // la dernière répétition est coupée, pas comprimée
    }
}
} // namespace

std::vector<float> nineSliceVertices(const ImageStyle &image, const Rect &rect)
{
    std::vector<float> out;
    if (!image.image || image.image->width == 0 || image.image->height == 0 || rect.size.x <= 0.0f || rect.size.y <= 0.0f)
        return out;
    const float W = static_cast<float>(image.image->width);
    const float H = static_cast<float>(image.image->height);
    const float s = image.scale;
    float L = image.left * s, R = image.right * s, T = image.top * s, B = image.bottom * s;
    if (L + R > rect.size.x)
    {
        const float f = rect.size.x / (L + R);
        L *= f;
        R *= f;
    }
    if (T + B > rect.size.y)
    {
        const float f = rect.size.y / (T + B);
        T *= f;
        B *= f;
    }
    const float x0 = rect.position.x, x1 = x0 + L, x3 = x0 + rect.size.x, x2 = x3 - R;
    const float y0 = rect.position.y, y1 = y0 + T, y3 = y0 + rect.size.y, y2 = y3 - B;
    const float sl = image.left, sr = W - image.right, st = image.top, sb = H - image.bottom;
    const Writer w{out, W, H};

    // Coins.
    w.quad(x0, y0, x1, y1, 0, 0, sl, st);
    w.quad(x2, y0, x3, y1, sr, 0, W, st);
    w.quad(x0, y2, x1, y3, 0, sb, sl, H);
    w.quad(x2, y2, x3, y3, sr, sb, W, H);
    // Bords : haut et bas le long de x, gauche et droite le long de y.
    tile(image.edges, x1, x2, sl, sr, s, [&](float a, float b, float u0, float u1) {
        w.quad(a, y0, b, y1, u0, 0, u1, st);
        w.quad(a, y2, b, y3, u0, sb, u1, H);
    });
    tile(image.edges, y1, y2, st, sb, s, [&](float a, float b, float v0, float v1) {
        w.quad(x0, a, x1, b, 0, v0, sl, v1);
        w.quad(x2, a, x3, b, sr, v0, W, v1);
    });
    // Centre : sur les deux axes.
    tile(image.center, y1, y2, st, sb, s, [&](float a, float b, float v0, float v1) {
        tile(image.center, x1, x2, sl, sr, s, [&](float c, float d, float u0, float u1) { w.quad(c, a, d, b, u0, v0, u1, v1); });
    });
    return out;
}

void appendNineSlice(renderer::DrawList &list, const ImageStyle &image, const Rect &rect, const Color &tint)
{
    std::vector<float> vertices = nineSliceVertices(image, rect);
    if (vertices.empty())
        return;
    list.image(renderer::DrawImage{.image = image.image, .vertices = std::move(vertices), .tint = tint});
}

} // namespace orosmo::ui
