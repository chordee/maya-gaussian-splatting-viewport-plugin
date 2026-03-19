#pragma once
#include <vector>
#include <string>

struct SplatData {
    // Structure of Arrays (SoA) for bulletproof SSBO alignment
    std::vector<float> positions; // x, y, z, 1.0 (vec4)
    std::vector<float> rotations; // w, x, y, z (vec4)
    std::vector<float> scales;    // sx, sy, sz, opacity (vec4)
    std::vector<float> sh_dc;     // r, g, b, 0.0 (vec4)

    int splatCount = 0;
    bool load(const std::string& path, std::string& errorOut);
};
