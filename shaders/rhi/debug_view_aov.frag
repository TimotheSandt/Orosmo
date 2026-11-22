#version 450
// REN-35 : affiche un AOV de la scène (avec blit.vert). OROSMO_VIEW : 1 normales (octaédrique), 2 vecteurs de mouvement.
#include "../include/orosmo/coords.glsl"

layout(set = 1, binding = 0) uniform sampler2D source;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

void main()
{
    const vec2 value = texture(source, uv).rg;
#if OROSMO_VIEW == 1
    if (value.x < 0.0 || value.y < 0.0) // valeur d'effacement : pas de géométrie
    {
        FragColor = vec4(0.05, 0.05, 0.07, 1.0);
        return;
    }
    FragColor = vec4(octDecode(value) * 0.5 + 0.5, 1.0);
#else
    // Gris moyen = immobile ; l'écart à ce gris donne la direction (x : rouge, y : vert), 10 = déplacement de 10 % de l'écran.
    FragColor = vec4(0.5 + value * 10.0, 0.5, 1.0);
#endif
}
