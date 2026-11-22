#version 450
// REN-35 : remplace mesh.frag pour les modes d'affichage de debug (avec mesh.vert). OROSMO_DEBUG_VIEW : 1 albédo, 2 surdessin, 3 filaire.
layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 crntPos;
layout(location = 2) in vec3 color;

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform Push
{
    int wireframe; // convention des programmes de maillage : 1 = passage des arêtes
} pc;

void main()
{
#if OROSMO_DEBUG_VIEW == 1
    FragColor = vec4(color, 1.0);
#elif OROSMO_DEBUG_VIEW == 2
    FragColor = vec4(vec3(1.0 / 8.0), 1.0 / 8.0); // additionné : un huitième par couche
#else
    FragColor = pc.wireframe == 1 ? vec4(1.0) : vec4(0.07, 0.07, 0.09, 1.0);
#endif
}
