// Boîte à outils de shaders (REN-22) : maths de base. Fonctions pures : aucun uniform, aucune liaison, utilisables dans
// tous les étages. Inclusion : #include "../include/orosmo/math.glsl" (relatif au shader) ou "orosmo/math.glsl" si le dossier
// `shaders/include` est une racine d'inclusion de ShaderLibrary.
#ifndef OROSMO_MATH_GLSL
#define OROSMO_MATH_GLSL

const float kPi = 3.14159265358979323846;
const float kTwoPi = 6.28318530717958647692;
const float kHalfPi = 1.57079632679489661923;
const float kInvPi = 0.31830988618379067154;
const float kEpsilon = 1e-6;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec2 saturate(vec2 x) { return clamp(x, vec2(0.0), vec2(1.0)); }
vec3 saturate(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }

float sqr(float x) { return x * x; }
vec3 sqr(vec3 x) { return x * x; }
float pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

// Remappe x de [a, b] vers [c, d] sans borner ; b == a donne c.
float remap(float x, float a, float b, float c, float d)
{
    float span = b - a;
    float t = span != 0.0 ? (x - a) / span : 0.0; // tout intervalle non nul est remappé, même très petit
    return mix(c, d, t);
}

// Division sûre : 0 quand le dénominateur est quasi nul.
float safeDiv(float a, float b) { return abs(b) > kEpsilon ? a / b : 0.0; }

vec3 safeNormalize(vec3 v)
{
    float l2 = dot(v, v);
    return l2 > kEpsilon * kEpsilon ? v * inversesqrt(l2) : vec3(0.0);
}

float luminance(vec3 linearRgb) { return dot(linearRgb, vec3(0.2126, 0.7152, 0.0722)); }

// sRGB <-> linéaire (courbe exacte par morceaux).
vec3 srgbToLinear(vec3 c)
{
    // pow n'est évalué que sur une base >= 0 (pow d'un négatif est indéfini, et mix propagerait le NaN de la branche écartée).
    return mix(c / 12.92, pow(max((c + 0.055) / 1.055, vec3(0.0)), vec3(2.4)), step(vec3(0.04045), c));
}
vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// Base orthonormée (t, b) perpendiculaire à n (unitaire) : Duff et al., sans branchement instable.
void orthonormalBasis(vec3 n, out vec3 t, out vec3 b)
{
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}

#endif
