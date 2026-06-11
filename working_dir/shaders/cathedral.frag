#version 430

layout(location = 0) in vec2 fUV;
layout(location = 1) in vec3 fNormal;
layout(location = 2) in vec3 fWorldPos;

layout(location = 0) out vec4 fboColor;

uniform sampler2D tAlbedo;
uniform vec3 uViewPos;

void main() {
    vec3 color = texture(tAlbedo, fUV).rgb;
    
    // Simple directional light
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(fNormal);
    
    float diff = max(dot(normal, lightDir), 0.2);
    vec3 diffuse = diff * color;
    
    // Ambient lighting
    vec3 ambient = 0.15 * color;
    
    vec3 finalColor = ambient + diffuse;
    
    fboColor = vec4(finalColor, 1.0);
}
