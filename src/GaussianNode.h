#pragma once
#include <maya/MPxLocatorNode.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include "PlyLoader.h"

class GaussianNode : public MPxLocatorNode {
public:
    static MTypeId  typeId;
    static MString  typeName;
    static MString  drawDBClassification;
    static MString  drawRegistrantId;

    static MObject  aFilePath;
    static MObject  aSplatScale;
    static MObject  aOpacityMult;

    SplatData       splatData;
    bool            dirty = true;
    MString         loadedPath;

    static void*    creator();
    static MStatus  initialize();

    void            postConstructor() override;
    MStatus         compute(const MPlug&, MDataBlock&) override;
    bool            isBounded() const override { return true; }
    MBoundingBox    boundingBox() const override;

    MStatus         setDependentsDirty(const MPlug& plug, MPlugArray& plugArray) override;
};
