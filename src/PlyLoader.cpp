#include "PlyLoader.h"
#include "tinyply.h"
#include <fstream>
#include <cmath>
#include <iostream>
#include <maya/MGlobal.h>
#include <cstring>

bool SplatData::load(const std::string& path, std::string& errorOut) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) { errorOut = "Cannot open file: " + path; return false; }

        tinyply::PlyFile ply;
        ply.parse_header(file);

        std::shared_ptr<tinyply::PlyData> ply_pos, ply_rot, ply_scale, ply_opac, ply_sh_dc;
        std::shared_ptr<tinyply::PlyData> ply_sh_rest;

        try { ply_pos = ply.request_properties_from_element("vertex", {"x", "y", "z"}); } catch (...) { errorOut = "Missing vertex x,y,z"; return false; }
        try { ply_rot = ply.request_properties_from_element("vertex", {"rot_0", "rot_1", "rot_2", "rot_3"}); } catch (...) { errorOut = "Missing vertex rot_0..3"; return false; }
        try { ply_scale = ply.request_properties_from_element("vertex", {"scale_0", "scale_1", "scale_2"}); } catch (...) { errorOut = "Missing vertex scale_0..2"; return false; }
        try { ply_opac = ply.request_properties_from_element("vertex", {"opacity"}); } catch (...) { errorOut = "Missing vertex opacity"; return false; }
        try { ply_sh_dc = ply.request_properties_from_element("vertex", {"f_dc_0", "f_dc_1", "f_dc_2"}); } catch (...) { errorOut = "Missing vertex f_dc_0..2"; return false; }

        // Probe for f_rest coefficients: try degree 3 (45), 2 (24), 1 (9) in order.
        // The 3DGS PLY format stores SH per channel: all R coefficients first, then G, then B.
        // Channel stride = 3 (deg1) | 3+5=8 (deg2) | 3+5+7=15 (deg3).
        int restCount = 0;
        {
            auto try_rest = [&](int n) -> bool {
                std::vector<std::string> names;
                names.reserve(n);
                for (int k = 0; k < n; ++k)
                    names.push_back("f_rest_" + std::to_string(k));
                try {
                    ply_sh_rest = ply.request_properties_from_element("vertex", names);
                    restCount = n;
                    return true;
                } catch (...) { return false; }
            };
            if (!try_rest(45)) if (!try_rest(24)) try_rest(9);
        }

        ply.read(file);

        if (!ply_pos || !ply_rot || !ply_scale || !ply_opac || !ply_sh_dc) {
            errorOut = "Failed to read required PLY elements.";
            return false;
        }

        const size_t N = ply_pos->count;
        splatCount = static_cast<int>(N);

        positions.assign(N * 4, 0.0f);
        rotations.assign(N * 4, 0.0f);
        scales.assign(N * 4, 0.0f);
        sh_dc.assign(N * 4, 0.0f);
        sh_rest1.clear();
        shDegree = 0;

        // Channel stride: how many coefficients per color channel
        // (determines where G and B degree-1 coefficients start).
        int chStride = (restCount == 45) ? 15 : (restCount == 24) ? 8 : (restCount == 9) ? 3 : 0;
        if (chStride >= 3 && ply_sh_rest) {
            sh_rest1.assign(N * 9, 0.0f);
            shDegree = 1;
        }

        const float* P_ptr  = reinterpret_cast<const float*>(ply_pos->buffer.get());
        const float* R_ptr  = reinterpret_cast<const float*>(ply_rot->buffer.get());
        const float* S_ptr  = reinterpret_cast<const float*>(ply_scale->buffer.get());
        const float* O_ptr  = reinterpret_cast<const float*>(ply_opac->buffer.get());
        const float* DC_ptr = reinterpret_cast<const float*>(ply_sh_dc->buffer.get());
        const float* SR_ptr = (shDegree >= 1 && ply_sh_rest)
                              ? reinterpret_cast<const float*>(ply_sh_rest->buffer.get())
                              : nullptr;

        for (size_t i = 0; i < N; ++i) {
            positions[i*4 + 0] = P_ptr[i*3 + 0];
            positions[i*4 + 1] = P_ptr[i*3 + 1];
            positions[i*4 + 2] = P_ptr[i*3 + 2];
            positions[i*4 + 3] = 1.0f;

            rotations[i*4 + 0] = R_ptr[i*4 + 0];
            rotations[i*4 + 1] = R_ptr[i*4 + 1];
            rotations[i*4 + 2] = R_ptr[i*4 + 2];
            rotations[i*4 + 3] = R_ptr[i*4 + 3];

            scales[i*4 + 0] = std::exp(S_ptr[i*3 + 0]);
            scales[i*4 + 1] = std::exp(S_ptr[i*3 + 1]);
            scales[i*4 + 2] = std::exp(S_ptr[i*3 + 2]);
            scales[i*4 + 3] = 1.0f / (1.0f + std::exp(-O_ptr[i]));

            sh_dc[i*4 + 0] = DC_ptr[i*3 + 0];
            sh_dc[i*4 + 1] = DC_ptr[i*3 + 1];
            sh_dc[i*4 + 2] = DC_ptr[i*3 + 2];
            sh_dc[i*4 + 3] = 1.0f;

            if (SR_ptr) {
                // Each color channel occupies chStride coefficients.
                // Degree-1 coefficients are the first 3 of each channel's block.
                const float* rBase = SR_ptr + i * restCount;
                // Y_{1,-1}: RGB across channels at offset 0
                sh_rest1[i*9 + 0] = rBase[0];
                sh_rest1[i*9 + 1] = rBase[chStride];
                sh_rest1[i*9 + 2] = rBase[chStride * 2];
                // Y_{1,0}: RGB at offset 1
                sh_rest1[i*9 + 3] = rBase[1];
                sh_rest1[i*9 + 4] = rBase[chStride + 1];
                sh_rest1[i*9 + 5] = rBase[chStride * 2 + 1];
                // Y_{1,1}: RGB at offset 2
                sh_rest1[i*9 + 6] = rBase[2];
                sh_rest1[i*9 + 7] = rBase[chStride + 2];
                sh_rest1[i*9 + 8] = rBase[chStride * 2 + 2];
            }
        }
        
        MGlobal::displayInfo(MString("[GaussianSplat] Loaded ") + splatCount
            + " splats, SH degree=" + shDegree);

        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}
