#version 450
// Copie d'une texture sur la cible courante (filtrage de l'échantillonneur lié).
layout(set = 1, binding = 0) uniform sampler2D source;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = texture(source, uv);
}
