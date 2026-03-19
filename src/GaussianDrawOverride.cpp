#include <GL/glew.h>
#include "GaussianDrawOverride.h"
#include "GaussianNode.h"
#include <maya/MFnDependencyNode.h>
#include <maya/MDrawContext.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MPoint.h>

MHWRender::MPxDrawOverride* GaussianDrawOverride::creator(const MObject& obj) {
    MGlobal::displayInfo("[GaussianSplat] GaussianDrawOverride::creator called.");
    return new GaussianDrawOverride(obj);
}

GaussianDrawOverride::GaussianDrawOverride(const MObject& obj) 
    : MHWRender::MPxDrawOverride(obj, GaussianDrawOverride::draw, true) {}

GaussianDrawOverride::~GaussianDrawOverride() {}

MHWRender::DrawAPI GaussianDrawOverride::supportedDrawAPIs() const {
    return MHWRender::kOpenGL | MHWRender::kOpenGLCoreProfile;
}

MUserData* GaussianDrawOverride::prepareForDraw(
    const MDagPath& objPath,
    const MDagPath& cameraPath,
    const MHWRender::MFrameContext& ctx,
    MUserData* oldData) 
{
    GaussianUserData* data = dynamic_cast<GaussianUserData*>(oldData);
    if (!data) data = new GaussianUserData();

    MObject node = objPath.node();
    MFnDependencyNode fn(node);
    GaussianNode* gNode = dynamic_cast<GaussianNode*>(fn.userNode());
    if (!gNode) return data;

    MString path = fn.findPlug(GaussianNode::aFilePath, false).asString();
    if (path != lastPath_ || gNode->dirty) {
        std::string err;
        if (gNode->splatData.load(path.asChar(), err)) {
            lastPath_ = path;
            gNode->dirty = false;
        } else if (path.length() > 0) {
            MGlobal::displayError(MString("[GaussianSplat] Load failed: ") + err.c_str());
        }
    }
    
    data->renderer = &renderer_;
    
    if (gNode->splatData.splatCount != lastSplatCount_) {
        renderer_.setPendingData(gNode->splatData);
        lastSplatCount_ = gNode->splatData.splatCount;
    }

    data->splatScale = fn.findPlug(GaussianNode::aSplatScale, false).asFloat();
    data->opacityMult = fn.findPlug(GaussianNode::aOpacityMult, false).asFloat();
    data->shDegree = 1;

    return data;
}

void GaussianDrawOverride::draw(const MHWRender::MDrawContext& ctx, const MUserData* data) {
    const GaussianUserData* gData = dynamic_cast<const GaussianUserData*>(data);
    if (!gData || !gData->renderer) return;

    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullEnabled  = glIsEnabled(GL_CULL_FACE);
    GLint depthMask, blendSrcRGB, blendDstRGB, blendSrcA, blendDstA;
    glGetIntegerv(GL_DEPTH_WRITEMASK,       &depthMask);
    glGetIntegerv(GL_BLEND_SRC_RGB,         &blendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,         &blendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,       &blendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA,       &blendDstA);

    // Enable depth test using Maya's existing depth function (do NOT override it —
    // Maya may use reversed-Z with GL_GREATER; overriding to GL_LESS breaks everything).
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);      // No depth writes for transparency
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // pre-multiplied alpha

    gData->renderer->draw(
        ctx,
        gData->splatScale,
        gData->opacityMult,
        gData->shDegree
    );

    if (!blendEnabled) glDisable(GL_BLEND);
    if (!depthEnabled) glDisable(GL_DEPTH_TEST);
    if (cullEnabled)   glEnable(GL_CULL_FACE);
    glDepthMask(depthMask);
    glBlendFuncSeparate(blendSrcRGB, blendDstRGB, blendSrcA, blendDstA);
}
