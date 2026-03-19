#include "GaussianNode.h"
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnStringData.h>
#include <maya/MGlobal.h>
#include <maya/MBoundingBox.h>
#include <maya/MDataHandle.h>
#include <maya/MViewport2Renderer.h>
#include <limits>
#include <algorithm>

MTypeId GaussianNode::typeId(0x0013BE00);
MString GaussianNode::typeName("gaussianSplatNode");
MString GaussianNode::drawDBClassification("drawdb/geometry/locator/gaussianSplat");
MString GaussianNode::drawRegistrantId("gaussianSplatDrawOverride");

MObject GaussianNode::aFilePath;
MObject GaussianNode::aSplatScale;
MObject GaussianNode::aOpacityMult;

void* GaussianNode::creator() { 
    MGlobal::displayInfo("--------------------------------------------------");
    MGlobal::displayInfo("[GaussianSplat] CRITICAL: GaussianNode::creator()");
    MGlobal::displayInfo("--------------------------------------------------");
    return new GaussianNode; 
}

MStatus GaussianNode::initialize() {
    MFnTypedAttribute   tAttr;
    MFnNumericAttribute nAttr;
    MFnStringData       strData;

    MObject defaultStr = strData.create("");
    aFilePath = tAttr.create("filePath", "fp", MFnData::kString, defaultStr);
    tAttr.setKeyable(false);
    tAttr.setStorable(true);
    tAttr.setUsedAsFilename(true); // This tells Maya it's a file path

    aSplatScale = nAttr.create("splatScale", "ss", MFnNumericData::kFloat, 1.0);
    nAttr.setKeyable(true);
    nAttr.setMin(0.001); nAttr.setMax(1000.0);

    aOpacityMult = nAttr.create("opacityMult", "om", MFnNumericData::kFloat, 1.0);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0); nAttr.setMax(10.0);

    addAttribute(aFilePath);
    addAttribute(aSplatScale);
    addAttribute(aOpacityMult);

    return MS::kSuccess;
}

void GaussianNode::postConstructor() {
    // Locator default is usually enough
}

MStatus GaussianNode::compute(const MPlug& plug, MDataBlock& block) {
    return MS::kUnknownParameter;
}

MStatus GaussianNode::setDependentsDirty(const MPlug& plug, MPlugArray& plugArray) {
    if (plug == aFilePath) {
        MGlobal::displayInfo("[GaussianSplat] filePath attribute changed, setting dirty.");
        dirty = true;
        MHWRender::MRenderer::setGeometryDrawDirty(thisMObject());
    }
    return MPxNode::setDependentsDirty(plug, plugArray);
}

MBoundingBox GaussianNode::boundingBox() const {
    if (splatData.splatCount == 0) {
        // Return a visible box initially so Maya calls draw
        return MBoundingBox(MPoint(-10, -10, -10), MPoint(10, 10, 10));
    }

    float mn[3] = { FLT_MAX,  FLT_MAX,  FLT_MAX };
    float mx[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX };
    
    for (int i = 0; i < splatData.splatCount; ++i) {
        float x = splatData.positions[i*4 + 0];
        float y = splatData.positions[i*4 + 1];
        float z = splatData.positions[i*4 + 2];
        
        mn[0] = std::min(mn[0], x); mx[0] = std::max(mx[0], x);
        mn[1] = std::min(mn[1], y); mx[1] = std::max(mx[1], y);
        mn[2] = std::min(mn[2], z); mx[2] = std::max(mx[2], z);
    }
    MGlobal::displayInfo(MString("[GaussianSplat] Bounding box calculated: ") + mn[0] + "," + mn[1] + "," + mn[2] + " to " + mx[0] + "," + mx[1] + "," + mx[2]);
    return MBoundingBox(MPoint(mn[0], mn[1], mn[2]), MPoint(mx[0], mx[1], mx[2]));
}
