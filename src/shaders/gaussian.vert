#version 450

layout(std430, binding = 0) readonly buffer PosBuffer    { vec4 positions[]; };
layout(std430, binding = 1) readonly buffer RotBuffer    { vec4 rotations[]; };
layout(std430, binding = 2) readonly buffer SclBuffer    { vec4 scales[];    };
layout(std430, binding = 3) readonly buffer SHBuffer     { vec4 sh_dc[];     };
layout(std430, binding = 5) readonly buffer IndexBuffer  { uint indices[];   };
// SH rest coefficients (degrees 1..3) packed per splat:
//   stride = u_restFloatsPerSplat = 9 (deg1) | 24 (deg2) | 45 (deg3)
//   layout per splat: [Y_{1,-1}.rgb, Y_{1,0}.rgb, Y_{1,1}.rgb,
//                      Y_{2,-2..2}.rgb,
//                      Y_{3,-3..3}.rgb]
layout(std430, binding = 6) readonly buffer SHRestBuffer { float sh_rest[];  };

uniform mat4  u_wvm;
uniform mat4  u_pm;
uniform float u_splatScale;
uniform float u_opacityMult;
uniform ivec4 u_viewport;
uniform int   u_shDegree;            // 0 = DC only, up to 3 = full view-dependent
uniform int   u_restFloatsPerSplat;  // stride into sh_rest (9, 24, or 45)
uniform int   u_sRGBToLinear;        // non-zero → apply pow(color, 2.2) after clamp
uniform float u_gamma;               // always-on extra gamma applied after the sRGB stage (1.0 = no-op)
uniform vec3  u_camPos;              // camera position in world space

out vec2  v_uv;
out vec4  v_color;
out float v_opacity;

// SH basis constants (Condon-Shortley convention, matches INRIA 3DGS)
const float C0 = 0.28209479177387814;
const float C1 = 0.4886025119029199;
const float C2_0 =  1.0925484305920792;
const float C2_1 = -1.0925484305920792;
const float C2_2 =  0.31539156525252005;
const float C2_3 = -1.0925484305920792;
const float C2_4 =  0.5462742152960396;
const float C3_0 = -0.5900435899266435;
const float C3_1 =  2.890611442640554;
const float C3_2 = -0.4570457994644658;
const float C3_3 =  0.3731763325901154;
const float C3_4 = -0.4570457994644658;
const float C3_5 =  1.445305721320277;
const float C3_6 = -0.5900435899266435;

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
    vec3  dc   = sh_dc[splatIdx].xyz;

    // 1. Transform to View Space
    vec4 viewPos = u_wvm * vec4(pos, 1.0);
    if (viewPos.z > -0.1) {
        // Behind or too close — discard by placing outside far clip plane
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        v_uv = vec2(0.0); v_color = vec4(0.0); v_opacity = 0.0;
        return;
    }

    // 2. SH Color Evaluation
    vec3 color = C0 * dc;

    if (u_shDegree >= 1) {
        // View direction in world space (splat → camera)
        vec3 dir = normalize(u_camPos - pos);
        float dx = dir.x, dy = dir.y, dz = dir.z;
        uint base = splatIdx * uint(u_restFloatsPerSplat);

        vec3 sh1_0 = vec3(sh_rest[base+0], sh_rest[base+1], sh_rest[base+2]); // Y_{1,-1}
        vec3 sh1_1 = vec3(sh_rest[base+3], sh_rest[base+4], sh_rest[base+5]); // Y_{1, 0}
        vec3 sh1_2 = vec3(sh_rest[base+6], sh_rest[base+7], sh_rest[base+8]); // Y_{1, 1}

        color += (-C1 * dy) * sh1_0
               + ( C1 * dz) * sh1_1
               + (-C1 * dx) * sh1_2;

        if (u_shDegree >= 2) {
            float xx = dx*dx, yy = dy*dy, zz = dz*dz;
            float xy = dx*dy, yz = dy*dz, xz = dx*dz;

            vec3 sh2_0 = vec3(sh_rest[base+9],  sh_rest[base+10], sh_rest[base+11]); // Y_{2,-2}
            vec3 sh2_1 = vec3(sh_rest[base+12], sh_rest[base+13], sh_rest[base+14]); // Y_{2,-1}
            vec3 sh2_2 = vec3(sh_rest[base+15], sh_rest[base+16], sh_rest[base+17]); // Y_{2, 0}
            vec3 sh2_3 = vec3(sh_rest[base+18], sh_rest[base+19], sh_rest[base+20]); // Y_{2, 1}
            vec3 sh2_4 = vec3(sh_rest[base+21], sh_rest[base+22], sh_rest[base+23]); // Y_{2, 2}

            color += C2_0 * xy                  * sh2_0
                   + C2_1 * yz                  * sh2_1
                   + C2_2 * (2.0*zz - xx - yy)  * sh2_2
                   + C2_3 * xz                  * sh2_3
                   + C2_4 * (xx - yy)           * sh2_4;

            if (u_shDegree >= 3) {
                vec3 sh3_0 = vec3(sh_rest[base+24], sh_rest[base+25], sh_rest[base+26]); // Y_{3,-3}
                vec3 sh3_1 = vec3(sh_rest[base+27], sh_rest[base+28], sh_rest[base+29]); // Y_{3,-2}
                vec3 sh3_2 = vec3(sh_rest[base+30], sh_rest[base+31], sh_rest[base+32]); // Y_{3,-1}
                vec3 sh3_3 = vec3(sh_rest[base+33], sh_rest[base+34], sh_rest[base+35]); // Y_{3, 0}
                vec3 sh3_4 = vec3(sh_rest[base+36], sh_rest[base+37], sh_rest[base+38]); // Y_{3, 1}
                vec3 sh3_5 = vec3(sh_rest[base+39], sh_rest[base+40], sh_rest[base+41]); // Y_{3, 2}
                vec3 sh3_6 = vec3(sh_rest[base+42], sh_rest[base+43], sh_rest[base+44]); // Y_{3, 3}

                color += C3_0 * dy * (3.0*xx - yy)            * sh3_0
                       + C3_1 * xy * dz                       * sh3_1
                       + C3_2 * dy * (4.0*zz - xx - yy)       * sh3_2
                       + C3_3 * dz * (2.0*zz - 3.0*xx - 3.0*yy) * sh3_3
                       + C3_4 * dx * (4.0*zz - xx - yy)       * sh3_4
                       + C3_5 * dz * (xx - yy)                * sh3_5
                       + C3_6 * dx * (xx - 3.0*yy)            * sh3_6;
            }
        }
    }

    color = clamp(color + 0.5, 0.0, 1.0);

    // Optional sRGB → linear conversion (approximate, gamma 2.2). Useful when
    // the framebuffer expects linear and the trained SH coefficients carry
    // sRGB-encoded color, which would otherwise blend "too bright".
    if (u_sRGBToLinear != 0) {
        color = pow(color, vec3(2.2));
    }
    // Independent extra gamma curve (1.0 = no-op). Applied on top of the
    // sRGB stage regardless of whether that toggle is on.
    color = pow(color, vec3(u_gamma));

    // 3. Covariance Projection (EWA splatting)
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

    // 3.0σ covers 99.7% of the Gaussian mass (3-sigma ellipse).
    // max(0.1, ...) prevents a degenerate zero-size splat from collapsing to a point.
    float r1 = 3.0 * u_splatScale * sqrt(max(0.1, lambda1));
    float r2 = 3.0 * u_splatScale * sqrt(max(0.1, lambda2));
    vec2 offsetPixels = quad.x * r1 * v1 + quad.y * r2 * v2;

    vec4 clipCenter = u_pm * viewPos;
    clipCenter.xy  += (offsetPixels / vec2(float(u_viewport[2]), float(u_viewport[3])))
                      * 2.0 * clipCenter.w;

    gl_Position = clipCenter;
    v_uv        = quad;
    v_color     = vec4(color, 1.0);
    v_opacity   = opac * u_opacityMult;
}
