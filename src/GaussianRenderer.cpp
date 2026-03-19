#include "GaussianRenderer.h"
#include <maya/MGlobal.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <numeric>
#include <algorithm>
#include <mutex>
#include <cstring>

std::string GaussianRenderer::s_shaderDir;

GaussianRenderer::GaussianRenderer() {}

GaussianRenderer::~GaussianRenderer() {
    destroyGL();
}

void GaussianRenderer::initGL() {
    if (vao_ != 0) return;

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &posBuf_);
    glGenBuffers(1, &rotBuf_);
    glGenBuffers(1, &sclBuf_);
    glGenBuffers(1, &shBuf_);
    glGenBuffers(1, &sh1Buf_);
    glGenBuffers(1, &indexBuf_);
    glGenBuffers(1, &keyBuf_);

    buildShaderProgram();
    buildSortProgram();
    glBindVertexArray(0);
}

void GaussianRenderer::destroyGL() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (posBuf_) glDeleteBuffers(1, &posBuf_);
    if (rotBuf_) glDeleteBuffers(1, &rotBuf_);
    if (sclBuf_) glDeleteBuffers(1, &sclBuf_);
    if (shBuf_) glDeleteBuffers(1, &shBuf_);
    if (sh1Buf_) glDeleteBuffers(1, &sh1Buf_);
    if (indexBuf_) glDeleteBuffers(1, &indexBuf_);
    if (keyBuf_) glDeleteBuffers(1, &keyBuf_);
    if (drawProgram_) glDeleteProgram(drawProgram_);
    if (sortProgram_) glDeleteProgram(sortProgram_);
    if (depthProgram_) glDeleteProgram(depthProgram_);
    vao_ = 0;
}

void GaussianRenderer::uploadSplats(const SplatData& data) {
    initGL();
    splatCount_ = data.splatCount;
    hasSH1_     = data.shDegree >= 1 && !data.sh_rest1.empty();
    sortDirty_  = true; // force re-sort after new data
    if (splatCount_ == 0) return;

    // Compute next power-of-2 for bitonic sort — the sort must operate on n entries
    // where n >= splatCount_, otherwise out-of-bounds writes corrupt GPU memory.
    sortN_ = 1;
    while (sortN_ < (uint32_t)splatCount_) sortN_ <<= 1;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, posBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, data.positions.size() * sizeof(float), data.positions.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rotBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, data.rotations.size() * sizeof(float), data.rotations.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sclBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, data.scales.size() * sizeof(float), data.scales.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, shBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, data.sh_dc.size() * sizeof(float), data.sh_dc.data(), GL_STATIC_DRAW);

    // SH degree-1 buffer (binding 6): 9 floats per splat.
    // Upload a single zero float when SH1 is absent so the buffer object is valid.
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sh1Buf_);
    if (hasSH1_) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, data.sh_rest1.size() * sizeof(float),
                     data.sh_rest1.data(), GL_STATIC_DRAW);
    } else {
        float zero = 0.0f;
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float), &zero, GL_STATIC_DRAW);
    }

    // IndexBuffer: size = sortN_. Valid entries [0..splatCount_-1] = identity order.
    // Padding entries [splatCount_..sortN_-1] = index 0 (safe dummy, never drawn).
    std::vector<uint32_t> indices(sortN_, 0);
    std::iota(indices.begin(), indices.begin() + splatCount_, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, indexBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sortN_ * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);

    // KeyBuffer (depths): size = sortN_. Fill ALL entries with +INF so padding sorts to end.
    const float kInf = std::numeric_limits<float>::infinity();
    std::vector<float> depths(sortN_, kInf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, keyBuf_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sortN_ * sizeof(float), depths.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    MGlobal::displayInfo(MString("[GaussianSplat] Uploaded ") + splatCount_ + " splats (sortN=" + (int)sortN_ + ").");
}

void GaussianRenderer::sort(const MMatrix& wvm) {
    if (splatCount_ == 0) return;

    // Build float matrix and check whether the camera moved since last sort.
    float f_wvm[16];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) f_wvm[i*4+j] = (float)wvm[i][j];

    if (!sortDirty_) {
        bool changed = false;
        for (int k = 0; k < 16 && !changed; ++k)
            if (std::abs(f_wvm[k] - prevWVM_[k]) > 1e-6f) changed = true;
        if (!changed) return;
    }
    sortDirty_ = false;
    std::memcpy(prevWVM_, f_wvm, sizeof(f_wvm));

    // 1. Calculate Depths
    glUseProgram(depthProgram_);
    glUniformMatrix4fv(glGetUniformLocation(depthProgram_, "u_wvm"), 1, GL_FALSE, f_wvm);

    glUniform1ui(glGetUniformLocation(depthProgram_, "u_numSplats"), splatCount_);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, posBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, keyBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, indexBuf_);

    uint32_t numGroups = (splatCount_ + 255) / 256;
    glDispatchCompute(numGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // 2. GPU Bitonic Sort — iterative multi-pass over sortN_ entries
    if (!sortProgram_) return;

    glUseProgram(sortProgram_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, keyBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, indexBuf_);
    glUniform1ui(glGetUniformLocation(sortProgram_, "u_numSplats"), sortN_);

    uint32_t sortGroups = (sortN_ + 255) / 256;

    for (uint32_t p = 1; p < sortN_; p <<= 1) {
        for (uint32_t q = p; q >= 1; q >>= 1) {
            glUniform1ui(glGetUniformLocation(sortProgram_, "u_p"), p);
            glUniform1ui(glGetUniformLocation(sortProgram_, "u_q"), q);
            glDispatchCompute(sortGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }
}

void GaussianRenderer::draw(const MHWRender::MDrawContext& ctx, float splatScale, float opacityMult, int shDegree) {
    static std::once_flag glewOnce;
    std::call_once(glewOnce, []() {
        glewExperimental = GL_TRUE;
        GLenum err = glewInit();
        if (err != GLEW_OK)
            MGlobal::displayWarning(MString("[GaussianSplat] GLEW init: ") + (const char*)glewGetErrorString(err));
        else
            MGlobal::displayInfo("[GaussianSplat] GLEW initialized.");
    });

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (newDataAvailable_) {
            uploadSplats(pendingData_);
            pendingData_.positions.clear();
            newDataAvailable_ = false;
        }
    }

    if (!isReady()) return;

    if (drawProgram_ == 0) {
        MGlobal::displayError("[GaussianSplat] drawProgram_ is 0 — shaders failed to compile!");
        return;
    }

    MMatrix wvm = ctx.getMatrix(MHWRender::MFrameContext::kWorldViewMtx);
    MMatrix pm  = ctx.getMatrix(MHWRender::MFrameContext::kProjectionMtx);

    sort(wvm);

    float f_wvm[16], f_pm[16];
    for(int i=0; i<4; ++i) for(int j=0; j<4; ++j) {
        f_wvm[i*4+j] = (float)wvm[i][j];
        f_pm[i*4+j]  = (float)pm[i][j];
    }

    int x, y, w, h; ctx.getViewportDimensions(x, y, w, h);
    int viewport[4] = {x, y, w, h};

    glUseProgram(drawProgram_);
    // GL_FALSE: treat f_wvm as column-major. Since f_wvm is stored row-major (Maya convention),
    // GLSL receives the transpose of Maya's matrix, which is exactly what OpenGL column-vector
    // math needs: GLSL_mat = Maya_mat^T, so (GLSL_mat * col_vec) == (row_vec * Maya_mat).
    glUniformMatrix4fv(glGetUniformLocation(drawProgram_, "u_wvm"), 1, GL_FALSE, f_wvm);
    glUniformMatrix4fv(glGetUniformLocation(drawProgram_, "u_pm"),  1, GL_FALSE, f_pm);
    glUniform1f(glGetUniformLocation(drawProgram_, "u_splatScale"), splatScale);
    glUniform1f(glGetUniformLocation(drawProgram_, "u_opacityMult"), opacityMult);
    glUniform4iv(glGetUniformLocation(drawProgram_, "u_viewport"), 1, viewport);
    glUniform1i(glGetUniformLocation(drawProgram_, "u_shDegree"), hasSH1_ ? 1 : 0);

    // Camera position in world space = last column of inverse(WVM).
    MMatrix iwvm = wvm.inverse();
    float camPos[3] = { (float)iwvm[3][0], (float)iwvm[3][1], (float)iwvm[3][2] };
    glUniform3fv(glGetUniformLocation(drawProgram_, "u_camPos"), 1, camPos);

    glBindVertexArray(vao_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, posBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, rotBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sclBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, shBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, indexBuf_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, sh1Buf_);

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, splatCount_);

    // Check for GL errors after draw
    GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        static bool errReported = false;
        if (!errReported) {
            errReported = true;
            MGlobal::displayError(MString("[GaussianSplat] GL error after draw: ") + (int)glErr);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

GLuint GaussianRenderer::loadShader(GLenum type, const char* path) {
    std::ifstream file(path);
    if (!file) {
        MGlobal::displayError(MString("[GaussianSplat] Cannot open shader: ") + path);
        return 0;
    }
    std::stringstream ss; ss << file.rdbuf();
    std::string src = ss.str();
    const char* srcPtr = src.c_str();
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &srcPtr, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 0 ? len : 1, '\0');
        glGetShaderInfoLog(s, len, nullptr, &log[0]);
        MGlobal::displayError(MString("[GaussianSplat] Shader compile error (") + path + "):\n" + log.c_str());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint GaussianRenderer::linkProgram(GLuint vert, GLuint frag, GLuint comp) {
    GLuint p = glCreateProgram();
    if (vert) glAttachShader(p, vert);
    if (frag) glAttachShader(p, frag);
    if (comp) glAttachShader(p, comp);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 0 ? len : 1, '\0');
        glGetProgramInfoLog(p, len, nullptr, &log[0]);
        MGlobal::displayError(MString("[GaussianSplat] Program link error:\n") + log.c_str());
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void GaussianRenderer::buildShaderProgram() {
    std::string vPath = s_shaderDir + "gaussian.vert";
    std::string fPath = s_shaderDir + "gaussian.frag";
    GLuint v = loadShader(GL_VERTEX_SHADER, vPath.c_str());
    GLuint f = loadShader(GL_FRAGMENT_SHADER, fPath.c_str());
    if (v && f) drawProgram_ = linkProgram(v, f);
    if (v) glDeleteShader(v); if (f) glDeleteShader(f);
}

void GaussianRenderer::buildSortProgram() {
    std::string dPath = s_shaderDir + "depth.comp";
    std::string sPath = s_shaderDir + "sort.comp";
    GLuint d = loadShader(GL_COMPUTE_SHADER, dPath.c_str());
    if (d) depthProgram_ = linkProgram(0, 0, d);
    GLuint s = loadShader(GL_COMPUTE_SHADER, sPath.c_str());
    if (s) sortProgram_ = linkProgram(0, 0, s);
    if (d) glDeleteShader(d); if (s) glDeleteShader(s);
}
