#version 430

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec2 fUV;
layout(location = 1) out vec3 fNormal;
layout(location = 2) out vec3 fWorldPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMatrix;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    fUV = vUV;
    fNormal = normalize(uNormalMatrix * vNormal);
    fWorldPos = vec3(uModel * vec4(vPos, 1.0));
    gl_Position = uProj * uView * uModel * vec4(vPos, 1.0);
}
