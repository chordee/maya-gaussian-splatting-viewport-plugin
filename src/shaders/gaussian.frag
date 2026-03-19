#version 450

in  vec2  v_uv;
in  vec4  v_color;
in  float v_opacity;

out vec4  fragColor;

void main() {
    float r2 = dot(v_uv, v_uv);
    if (r2 > 1.0) discard;

    float alpha = v_opacity * exp(-0.5 * r2 * 9.0);
    if (alpha < 0.004) discard;

    // Pre-multiplied alpha output
    fragColor = vec4(v_color.rgb * alpha, alpha);
}
