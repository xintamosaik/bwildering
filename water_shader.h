#ifndef WATER_SHADER_H
#define WATER_SHADER_H

/* GLSL 330 surface shading only: block geometry and sea level remain fixed.
   Ripples perturb the lighting normal, with a soft sky reflection at grazing
   angles. No textures, framebuffer copies, or extra geometry are required. */
static const char WATER_SHADER_GLSL[] =
    "vec4 shadeWater(vec3 position, vec3 geometricNormal, vec4 cameraTime) {\n"
    "    vec3 grid = (position - blockGrid.xyz) * blockGrid.w;\n"
    "    float a = dot(grid.xz, vec2(0.65, 0.42)) + cameraTime.w * 0.65;\n"
    "    float b = dot(grid.xz, vec2(-0.37, 0.81)) - cameraTime.w * 0.48;\n"
    "    vec3 normal = normalize(geometricNormal + abs(geometricNormal.y) *\n"
    "        vec3(0.035 * cos(a) - 0.025 * cos(b), 0.0, 0.025 * cos(a) + 0.035 * cos(b)));\n"
    "    vec3 view = normalize(cameraTime.xyz - position);\n"
    "    float fresnel = 0.02 + 0.98 * pow(1.0 - abs(dot(normal, view)), 5.0);\n"
    "    vec3 sun = normalize(vec3(-0.4, 1.0, 0.3));\n"
    "    float sparkle = pow(max(dot(reflect(-sun, normal), view), 0.0), 96.0);\n"
    "    vec3 water = mix(vec3(0.075, 0.28, 0.33), vec3(0.43, 0.59, 0.69), fresnel);\n"
    "    water += vec3(0.28, 0.27, 0.23) * sparkle;\n"
    "    return vec4(water, mix(0.48, 0.86, fresnel));\n"
    "}\n";

#endif
