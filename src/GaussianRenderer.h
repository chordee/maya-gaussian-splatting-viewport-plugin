#pragma once
#include "PlyLoader.h"
#include <GL/glew.h>
#include <maya/MMatrix.h>
#include <maya/MPoint.h>
#include <maya/MDrawContext.h>
#include <string>
#include <vector>
#include <mutex>

class GaussianRenderer {
public:
    GaussianRenderer();
    ~GaussianRenderer();

    static void setShaderDir(const std::string& dir) { s_shaderDir = dir; }

    void setPendingData(const SplatData& data) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        pendingData_ = data;
        newDataAvailable_ = true;
    }

    void sort(const MMatrix& viewMatrix);   
    void draw(const MHWRender::MDrawContext& ctx,
              float splatScale,
              float opacityMult,
              int   shDegree);

    bool isReady() const { return vao_ != 0 && splatCount_ > 0; }
    int  splatCount() const { return splatCount_; }

private:
    void initGL();
    void buildShaderProgram();
    void buildSortProgram();
    void destroyGL();
    void uploadSplats(const SplatData& data);

    GLuint loadShader(GLenum type, const char* path);
    GLuint linkProgram(GLuint vert, GLuint frag, GLuint comp = 0);

    GLuint vao_         = 0;
    GLuint posBuf_      = 0;
    GLuint rotBuf_      = 0;
    GLuint sclBuf_      = 0;
    GLuint shBuf_       = 0;
    GLuint indexBuf_    = 0; // Sorted indices
    GLuint keyBuf_      = 0; // Depths for sorting
    GLuint drawProgram_ = 0;
    GLuint sortProgram_ = 0;
    GLuint depthProgram_ = 0; // Compute shader to calc depths
    GLuint quadVBO_     = 0;

    int      splatCount_ = 0;
    uint32_t sortN_      = 0; // next power-of-2 >= splatCount_, used for bitonic sort allocation

    SplatData pendingData_;
    bool      newDataAvailable_ = false;
    std::mutex dataMutex_;

    static std::string s_shaderDir;
};
