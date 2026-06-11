#version 430

layout(location = 0) in vec2 fUV;
layout(location = 1) in vec3 fNormal;
layout(location = 2) in vec3 fWorldPos;

layout(location = 0) out vec4 fboColor;

uniform vec3 uViewPos;
uniform sampler2D tFloorAlbedo;
uniform sampler2D tOpaqueColor;
uniform sampler2D tDepth;

uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uInvProj;
uniform mat4 uInvView;

uniform float uSSRStepSize;
uniform int uSSRMaxSteps;
uniform float uSSRThickness;
uniform int uDebugMode;
uniform float uSSRBlendFactor;

vec3 TraceSSR(vec3 viewPos, vec3 reflectDir, out bool hit) {
    hit = false;
    
    float stepSize = uSSRStepSize;
    int maxSteps = uSSRMaxSteps;
    float thickness = uSSRThickness;

    vec3 rayPos = viewPos;

    for (int i = 0; i < maxSteps; i++) {
        rayPos += reflectDir * stepSize;

        // Project ray position to screen space
        vec4 projPos = uProj * vec4(rayPos, 1.0);

        // If you are looking at a reflective wall, you can get rays that go behind the camera, so we need to check this before perspective divide
        if (projPos.w <= 0.0)
            break; // Behind the camera (w represents depth in clip space)

        projPos.xyz /= projPos.w; // shift to NDC space
        vec2 uv = projPos.xy * 0.5 + 0.5; // Convert to [0,1] UV coordinates to sample depth and color textures

        // Check if the UV coordinate is within screen bounds
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            break;
        }

        // Sample the scene depth at this UV (returns depth in [0,1] range, 1 means far)
        float depthVal = texture(tDepth, uv).r;

        // Reconstruct the view-space position of the geometry at this screen position (reversing what we did)
        float z_ndc = depthVal * 2.0 - 1.0; // Convert depth to NDC
        vec4 clipPos = vec4(uv * 2.0 - 1.0, z_ndc, 1.0); // NDC space
        vec4 geomViewPos = uInvProj * clipPos; // Back to view space
        geomViewPos.xyz /= geomViewPos.w; // Perspective divide to get actual view-space position

        // Compare depths in view space (rayPos.z is the depth of the ray, geomViewPos.z is the depth of the geometry at this screen position)
        // -Z is forward in view space
        if (rayPos.z <= geomViewPos.z && // Has the ray passed through the geometry plane?
            rayPos.z >= geomViewPos.z - thickness) // Is it a real hit within the thickness threshold? (it may hit the back face of the geometry, so we check a range)
        {
            if (depthVal < 1.0) { // It is a valid hit (not just sky/background because they are at depth 1.0)
                hit = true;
                return texture(tOpaqueColor, uv).rgb;
            }
        }
    }

    return vec3(0.0);
}

void main() {
    vec3 V = normalize(uViewPos - fWorldPos);
    vec3 N = normalize(fNormal);
    N = faceforward(N, -V, N); // Look at camera

    // Calculate view-space position and reflection direction
    vec3 viewPos = (uView * vec4(fWorldPos, 1.0)).xyz;
    vec3 viewNormal = normalize(mat3(uView) * fNormal);
    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, viewNormal));

    // Trace SSR
    bool hit = false;
    vec3 reflectionColor = vec3(0.0);
    
    if (reflectDir.z < 0.0) { // Ray is moving away from the camera in view space
        reflectionColor = TraceSSR(viewPos, reflectDir, hit);
    }

    if (!hit) {
        reflectionColor = vec3(0.15f, 0.20f, 0.30f); // base color for fallback (dark blue as in main.cpp glClearColor)

        // There is no problem with this approach in this scene because reflective surface is located inside the cathedral,
        // so we won't see the sky, but let's say we were doing SSR for a lake inside the forest that we could see in reflections,
        // then we could do something like this to create a simple sky gradient based on the reflection direction.
        
        // vec3 R = reflect(-V, N);
        // float t = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
        // reflectionColor = mix(someSkyColor, someGroundColor, t);
    }

    // Floor base color
    vec3 floorBaseColor = texture(tFloorAlbedo, fUV * 4.0).rgb;

    // Fresnel equation using Schlick's approximation
    float cosTheta = clamp(dot(V, N), 0.0, 1.0);
    float R0 = 0.04; // reflection at normal incidence
    float fresnel = R0 + (1.0 - R0) * pow(1.0 - cosTheta, 5.0);

    // Blend base texture and reflection color based on debug mode
    vec3 finalColor;
    if (uDebugMode == 1) {
        // Reflection only (without floor texture and without Fresnel attenuation)
        finalColor = reflectionColor;
    }
    else if (uDebugMode == 2) {
        // Show SSR hits (green for hits black for misses)
        if (hit)
            finalColor = vec3(0.0, 1.0, 0.0);
        else
            finalColor = vec3(0.0, 0.0, 0.0);
    }
    else {
        // Normal blend using Fresnel and custom blend factor
        finalColor = mix(floorBaseColor, reflectionColor, fresnel * uSSRBlendFactor);
    }

    fboColor = vec4(finalColor, 1.0);
}
