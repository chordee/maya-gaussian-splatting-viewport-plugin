#pragma once
#include <vector>
#include <string>

struct SplatData {
    // Structure of Arrays (SoA) for bulletproof SSBO alignment
    std::vector<float> positions; // x, y, z, 1.0 (vec4)
    std::vector<float> rotations; // w, x, y, z (vec4)
    std::vector<float> scales;    // sx, sy, sz, opacity (vec4)
    std::vector<float> sh_dc;     // r, g, b, 0.0 (vec4) — SH degree 0

    // SH rest coefficients (degrees 1..shDegree), interleaved per basis function:
    //   per splat:  [Y_{1,-1}.rgb, Y_{1,0}.rgb, Y_{1,1}.rgb,                       // 9  floats (deg 1)
    //                Y_{2,-2}.rgb, ..., Y_{2,2}.rgb,                                // 15 floats (deg 2)
    //                Y_{3,-3}.rgb, ..., Y_{3,3}.rgb]                                // 21 floats (deg 3)
    // restFloatsPerSplat = 9, 24, or 45 depending on shDegree.
    // Empty when the PLY has no f_rest properties (sh_degree 0 model).
    std::vector<float> sh_rest;
    int restFloatsPerSplat = 0;

    int splatCount = 0;
    int shDegree   = 0; // highest SH degree available (0, 1, 2, or 3)
    bool load(const std::string& path, std::string& errorOut);
};
