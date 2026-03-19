#pragma once
#include <maya/MPxDrawOverride.h>
#include <maya/MString.h>
#include <maya/MMatrix.h>
#include <maya/MPoint.h>
#include <maya/MUserData.h>
#include "GaussianRenderer.h"

class GaussianRenderer;

class GaussianUserData : public MUserData {
public:
    GaussianUserData() : MUserData(false), renderer(nullptr) {}
    ~GaussianUserData() override {}

    GaussianRenderer* renderer;
    float   mvp[16];
    float   mv[16];
    float   proj[16];
    float   camPos[3];
    int     viewport[4];
    float   splatScale;
    float   opacityMult;
    int     shDegree;
};

class GaussianDrawOverride : public MHWRender::MPxDrawOverride {
public:
    static MHWRender::MPxDrawOverride* creator(const MObject& obj);
    explicit GaussianDrawOverride(const MObject& obj);
    ~GaussianDrawOverride() override;

    MHWRender::DrawAPI supportedDrawAPIs() const override;

    bool hasUIDrawables() const override { return false; }

    MUserData* prepareForDraw(const MDagPath& objPath,
                              const MDagPath& cameraPath,
                              const MHWRender::MFrameContext& ctx,
                              MUserData* oldData) override;

    static void draw(const MHWRender::MDrawContext& ctx,
                     const MUserData* data);

private:
    GaussianRenderer renderer_;
    MString          lastPath_;
    size_t           lastSplatCount_ = 0;
};
