#version 450

layout(std430, binding = 0) readonly buffer PosBuffer   { vec4 positions[]; };
layout(std430, binding = 1) readonly buffer RotBuffer   { vec4 rotations[]; };
layout(std430, binding = 2) readonly buffer SclBuffer   { vec4 scales[];    };
layout(std430, binding = 3) readonly buffer SHBuffer    { vec4 sh_dc[];     };
layout(std430, binding = 5) readonly buffer IndexBuffer { uint indices[];   };

uniform mat4  u_wvm;
uniform mat4  u_pm;
uniform float u_splatScale;
uniform float u_opacityMult;
uniform ivec4 u_viewport;

out vec2  v_uv;
out vec4  v_color;
out float v_opacity;

mat3 quatToMat(vec4 q) {
    float w=q.x, x=q.y, y=q.z, z=q.w;
    return mat3(
        1.0-2.0*(y*y+z*z), 2.0*(x*y+w*z),   2.0*(x*z-w*y),
        2.0*(x*y-w*z),   1.0-2.0*(x*x+z*z), 2.0*(y*z+w*x),
        2.0*(x*z+w*y),   2.0*(y*z-w*x),   1.0-2.0*(x*x+y*y)
    );
}

void main() {
    uint splatIdx = indices[gl_InstanceID];

    vec3  pos  = positions[splatIdx].xyz;
    vec4  rot  = rotations[splatIdx];
    vec3  scl  = scales[splatIdx].xyz;
    float opac = scales[splatIdx].w;
    vec3  sh   = sh_dc[splatIdx].xyz;

    // 1. Transform to View Space
    vec4 viewPos = u_wvm * vec4(pos, 1.0);
    if (viewPos.z > -0.1) {
        // Behind or too close — discard by placing outside far clip plane
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        v_uv = vec2(0.0); v_color = vec4(0.0); v_opacity = 0.0;
        return;
    }

    // 2. Covariance Projection (EWA splatting)
    mat3 R = quatToMat(rot);
    mat3 S = mat3(scl.x, 0.0, 0.0,  0.0, scl.y, 0.0,  0.0, 0.0, scl.z);
    mat3 Sigma = R * S * S * transpose(R);

    float dist    = -viewPos.z;
    float focal_x = u_pm[0][0] * float(u_viewport[2]) * 0.5;
    float focal_y = u_pm[1][1] * float(u_viewport[3]) * 0.5;

    mat3 J = mat3(
        focal_x / dist, 0.0, -(focal_x * viewPos.x) / (dist * dist),
        0.0, focal_y / dist, -(focal_y * viewPos.y) / (dist * dist),
        0.0, 0.0, 0.0
    );

    mat3 W     = mat3(u_wvm);
    mat3 T     = J * W;
    mat3 cov2d = T * Sigma * transpose(T);

    float a = cov2d[0][0] + 0.3;
    float b = cov2d[0][1];
    float c = cov2d[1][1] + 0.3;

    float det  = a * c - b * b;
    float mid  = 0.5 * (a + c);
    float term = sqrt(max(0.1, mid * mid - det));
    float lambda1 = mid + term;
    float lambda2 = mid - term;

    vec2 raw = vec2(b, lambda1 - a);
    vec2 v1  = length(raw) < 1e-4 ? vec2(1.0, 0.0) : normalize(raw);
    vec2 v2  = vec2(-v1.y, v1.x);

    uint vertIdx = uint(gl_VertexID % 6);
    vec2 corners[6] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1),
                              vec2(-1,-1), vec2(1,1),  vec2(-1,1));
    vec2 quad = corners[vertIdx];

    float r1 = 3.0 * u_splatScale * sqrt(max(0.1, lambda1));
    float r2 = 3.0 * u_splatScale * sqrt(max(0.1, lambda2));
    vec2 offsetPixels = quad.x * r1 * v1 + quad.y * r2 * v2;

    vec4 clipCenter = u_pm * viewPos;
    clipCenter.xy  += (offsetPixels / vec2(float(u_viewport[2]), float(u_viewport[3])))
                      * 2.0 * clipCenter.w;

    gl_Position = clipCenter;
    v_uv        = quad;
    v_color     = vec4(clamp(sh * 0.28209 + 0.5, 0.0, 1.0), 1.0);
    v_opacity   = opac * u_opacityMult;
}
