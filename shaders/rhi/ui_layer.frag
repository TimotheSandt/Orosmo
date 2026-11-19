#version 450
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 1, binding = 0) uniform sampler2D textureSampler;

layout(push_constant) uniform Push
{
    vec4 offsetScale;
    vec4 containerScroll;
    vec4 content;
    vec4 color;
} pc;

void main()
{
    // Partie visible du contenu, décalée par le défilement
    vec2 scrollUV = pc.containerScroll.zw / pc.content.xy;
    vec2 visibleRatio = pc.offsetScale.zw / pc.content.xy;
    vec2 scrolledUV = v_TexCoord * visibleRatio + scrollUV;

    // Rien à montrer hors de la couche
    if (scrolledUV.x < 0.0 || scrolledUV.x > 1.0 || scrolledUV.y < 0.0 || scrolledUV.y > 1.0)
    {
        discard;
    }

    // L'interface a l'origine en haut ; la couche est lue avec la convention bas-gauche : on retourne Y.
    vec2 sampleUV = vec2(scrolledUV.x, 1.0 - scrolledUV.y);
    vec4 texColor = texture(textureSampler, sampleUV);

    // La couche contient une couleur prémultipliée (rgb déjà multiplié par a). La couleur de fond du conteneur (non
    // prémultipliée) passe DERRIÈRE le contenu : « contenu par-dessus fond », en prémultiplié, alpha compris (un fond opaque
    // donne un résultat opaque). La sortie est prémultipliée : le pipeline compose avec (1, 1 - a).
    vec4 background = vec4(pc.color.rgb * pc.color.a, pc.color.a);
    vec4 finalColor = texColor + background * (1.0 - texColor.a);

    if (finalColor.a < 0.01)
    {
        discard;
    }

    FragColor = finalColor;
}
