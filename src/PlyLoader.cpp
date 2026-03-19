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
        
        try { ply_pos = ply.request_properties_from_element("vertex", {"x", "y", "z"}); } catch (...) { errorOut = "Missing vertex x,y,z"; return false; }
        try { ply_rot = ply.request_properties_from_element("vertex", {"rot_0", "rot_1", "rot_2", "rot_3"}); } catch (...) { errorOut = "Missing vertex rot_0..3"; return false; }
        try { ply_scale = ply.request_properties_from_element("vertex", {"scale_0", "scale_1", "scale_2"}); } catch (...) { errorOut = "Missing vertex scale_0..2"; return false; }
        try { ply_opac = ply.request_properties_from_element("vertex", {"opacity"}); } catch (...) { errorOut = "Missing vertex opacity"; return false; }
        try { ply_sh_dc = ply.request_properties_from_element("vertex", {"f_dc_0", "f_dc_1", "f_dc_2"}); } catch (...) { errorOut = "Missing vertex f_dc_0..2"; return false; }

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

        const float* P_ptr  = reinterpret_cast<const float*>(ply_pos->buffer.get());
        const float* R_ptr  = reinterpret_cast<const float*>(ply_rot->buffer.get());
        const float* S_ptr  = reinterpret_cast<const float*>(ply_scale->buffer.get());
        const float* O_ptr  = reinterpret_cast<const float*>(ply_opac->buffer.get());
        const float* DC_ptr = reinterpret_cast<const float*>(ply_sh_dc->buffer.get());

        for (size_t i = 0; i < N; ++i) {
            // Keep EXACT raw data from PLY
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
        }
        
        if (N > 0) {
            MString msg;
            msg.format("[GaussianSplat] Splat[0] Pos: ^1s, ^2s, ^3s", 
                       MString() + (double)positions[0], 
                       MString() + (double)positions[1], 
                       MString() + (double)positions[2]);
            MGlobal::displayInfo(msg);
        }

        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}
