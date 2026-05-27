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

        // Defensive: tinyply silently leaves buffers empty when read() can't
        // populate them (truncated file, format mismatch). Dereferencing a
        // null buffer would crash Maya without warning.
        if (!ply_pos->buffer.get() || !ply_rot->buffer.get() ||
            !ply_scale->buffer.get() || !ply_opac->buffer.get() ||
            !ply_sh_dc->buffer.get()) {
            errorOut = "PLY element buffers are empty (truncated or malformed file?).";
            return false;
        }

        const size_t N = ply_pos->count;
        splatCount = static_cast<int>(N);

        positions.assign(N * 4, 0.0f);
        rotations.assign(N * 4, 0.0f);
        scales.assign(N * 4, 0.0f);
        sh_dc.assign(N * 4, 0.0f);
        sh_rest.clear();
        restFloatsPerSplat = 0;
        shDegree = 0;

        // Channel stride: coefficients per color channel in the PLY layout.
        // (deg 1 → 3, deg 2 → 8, deg 3 → 15)
        int chStride = (restCount == 45) ? 15 : (restCount == 24) ? 8 : (restCount == 9) ? 3 : 0;
        if (chStride >= 3 && ply_sh_rest) {
            shDegree = (chStride == 15) ? 3 : (chStride == 8) ? 2 : 1;
            restFloatsPerSplat = chStride * 3; // 9, 24, or 45
            sh_rest.assign(N * restFloatsPerSplat, 0.0f);
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

            // Normalize the quaternion — some training pipelines emit slightly
            // non-unit quaternions, which the vertex shader would otherwise turn
            // into anisotropic scaling on top of the splat covariance.
            float qw = R_ptr[i*4 + 0];
            float qx = R_ptr[i*4 + 1];
            float qy = R_ptr[i*4 + 2];
            float qz = R_ptr[i*4 + 3];
            float qlen = std::sqrt(qw*qw + qx*qx + qy*qy + qz*qz);
            if (qlen > 1e-8f) {
                float inv = 1.0f / qlen;
                qw *= inv; qx *= inv; qy *= inv; qz *= inv;
            } else {
                qw = 1.0f; qx = qy = qz = 0.0f;
            }
            rotations[i*4 + 0] = qw;
            rotations[i*4 + 1] = qx;
            rotations[i*4 + 2] = qy;
            rotations[i*4 + 3] = qz;

            scales[i*4 + 0] = std::exp(S_ptr[i*3 + 0]);
            scales[i*4 + 1] = std::exp(S_ptr[i*3 + 1]);
            scales[i*4 + 2] = std::exp(S_ptr[i*3 + 2]);
            scales[i*4 + 3] = 1.0f / (1.0f + std::exp(-O_ptr[i]));

            sh_dc[i*4 + 0] = DC_ptr[i*3 + 0];
            sh_dc[i*4 + 1] = DC_ptr[i*3 + 1];
            sh_dc[i*4 + 2] = DC_ptr[i*3 + 2];
            sh_dc[i*4 + 3] = 1.0f;

            if (SR_ptr) {
                // PLY layout per splat: [R0..R(chStride-1), G0..G(chStride-1), B0..B(chStride-1)]
                // Interleaved layout per splat: for each basis k → R,G,B triplet
                const float* rBase = SR_ptr + i * restCount;
                float*       dst   = &sh_rest[i * restFloatsPerSplat];
                for (int k = 0; k < chStride; ++k) {
                    dst[k*3 + 0] = rBase[k];
                    dst[k*3 + 1] = rBase[k + chStride];
                    dst[k*3 + 2] = rBase[k + chStride * 2];
                }
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
