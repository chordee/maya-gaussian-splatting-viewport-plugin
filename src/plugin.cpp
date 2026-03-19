#include <GL/glew.h>
#include <maya/MFnPlugin.h>
#include <maya/MStatus.h>
#include <maya/MObject.h>
#include <maya/MDrawRegistry.h>
#include <maya/MGlobal.h>
#include "GaussianNode.h"
#include "GaussianDrawOverride.h"
#include "GaussianRenderer.h"
#include <string>
#include <algorithm>

MStatus initializePlugin(MObject obj) {
    MGlobal::displayInfo("[GaussianSplat] --- Plugin Loading (Built: " __DATE__ " " __TIME__ ") ---");

    MFnPlugin plugin(obj, "Moonshine", "1.0", "Any");

    // Resolve shader directory relative to the plugin (.mll) location
    std::string pluginDir = plugin.loadPath().asChar();
    std::replace(pluginDir.begin(), pluginDir.end(), '\\', '/');
    if (!pluginDir.empty() && pluginDir.back() != '/') pluginDir += '/';
    GaussianRenderer::setShaderDir(pluginDir + "shaders/");
    MGlobal::displayInfo(MString("[GaussianSplat] Shader dir: ") + (pluginDir + "shaders/").c_str());

    MStatus s;
    s = plugin.registerNode(
        GaussianNode::typeName,
        GaussianNode::typeId,
        GaussianNode::creator,
        GaussianNode::initialize,
        MPxNode::kLocatorNode,
        &GaussianNode::drawDBClassification
    );
    if (!s) {
        MGlobal::displayError(MString("[GaussianSplat] Node registration failed: ") + s.errorString());
        return s;
    }

    s = MHWRender::MDrawRegistry::registerDrawOverrideCreator(
        GaussianNode::drawDBClassification,
        GaussianNode::drawRegistrantId,
        GaussianDrawOverride::creator
    );
    if (!s) {
        MGlobal::displayError(MString("[GaussianSplat] DrawOverride registration failed: ") + s.errorString());
        return s;
    }

    MGlobal::displayInfo(MString("[GaussianSplat] Successfully registered: ") + GaussianNode::drawDBClassification);

    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);
    
    MHWRender::MDrawRegistry::deregisterDrawOverrideCreator(
        GaussianNode::drawDBClassification,
        GaussianNode::drawRegistrantId
    );

    MStatus s = plugin.deregisterNode(GaussianNode::typeId);
    return s;
}
