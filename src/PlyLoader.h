#pragma once
#include <vector>
#include <string>

struct SplatData {
    // Structure of Arrays (SoA) for bulletproof SSBO alignment
    std::vector<float> positions; // x, y, z, 1.0 (vec4)
    std::vector<float> rotations; // w, x, y, z (vec4)
    std::vector<float> scales;    // sx, sy, sz, opacity (vec4)
    std::vector<float> sh_dc;     // r, g, b, 0.0 (vec4)
    // SH degree-1 coefficients (9 floats/splat): Y_{1,-1}, Y_{1,0}, Y_{1,1} × RGB.
    // Absent when the PLY has no f_rest properties (sh_degree 0 model).
    std::vector<float> sh_rest1;  // 9 floats per splat

    int splatCount = 0;
    int shDegree   = 0; // highest SH degree available (0 or 1)
    bool load(const std::string& path, std::string& errorOut);
};
