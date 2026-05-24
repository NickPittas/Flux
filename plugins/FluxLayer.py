# -*- coding: utf-8 -*-
# FluxLayer PyPlug — wraps Input -> FrameRange -> TimeOffset -> Transform -> Multiply -> Output
# Read node is EXTERNAL (created by C++), feeds into this gizmo's input.
#
# Stable parameter contract (C++ uses these exact names via getKnobByName):
#   frameRange   (Int2D: first, last)
#   timeOffset   (Int)
#   opacity      (RGBA — aliased to Multiply value, all 4 dims set uniformly for opacity)
#   translate    (Double2D)
#   scale        (Double2D)
#   uniform      (Boolean)
#   rotate       (Double)
#   skewX        (Double)
#   skewY        (Double)
#   skewOrder    (Choice)
#   center       (Double2D)
#   motionBlur   (Double)
#   shutter      (Double)
#   interactive  (Boolean)

import NatronEngine

def getPluginID():
    return "net.sf.openfx.FluxLayer"

def getLabel():
    return "FluxLayer"

def getVersion():
    return 1

def getIconPath():
    return ""

def getGrouping():
    return "Flux"

def getDescription():
    return "Flux layer gizmo: Input -> FrameRange -> TimeOffset -> Transform -> Multiply -> Output"

def createInstance(app, group):

    # ====================================================================
    # 1. Create group-level (stable) parameters
    # ====================================================================

    page = group.createPageParam("LayerSettings", "Layer Settings")

    # -- Frame Range (Int2D: dim 0 = first frame, dim 1 = last frame) --
    frameRangeParam = group.createInt2DParam("frameRange", "Frame Range")
    frameRangeParam.setDefaultValue(1, 0)
    frameRangeParam.setDefaultValue(1, 1)
    page.addParam(frameRangeParam)

    # -- Time Offset (Int) --
    timeOffsetParam = group.createIntParam("timeOffset", "Time Offset (Frames)")
    timeOffsetParam.setDefaultValue(0, 0)
    page.addParam(timeOffsetParam)

    # -- Before Behavior (Choice: original/hold/black/loop/bounce) --
    beforeParam = group.createChoiceParam("before", "Before Behavior")
    beforeParam.addOption("original", "")
    beforeParam.addOption("hold", "")
    beforeParam.addOption("black", "")
    beforeParam.addOption("loop", "")
    beforeParam.addOption("bounce", "")
    beforeParam.setDefaultValue(2)  # black
    beforeParam.restoreDefaultValue()
    beforeParam.setAnimationEnabled(True)
    page.addParam(beforeParam)

    # -- After Behavior (Choice: original/hold/black/loop/bounce) --
    afterParam = group.createChoiceParam("after", "After Behavior")
    afterParam.addOption("original", "")
    afterParam.addOption("hold", "")
    afterParam.addOption("black", "")
    afterParam.addOption("loop", "")
    afterParam.addOption("bounce", "")
    afterParam.setDefaultValue(2)  # black
    afterParam.restoreDefaultValue()
    afterParam.setAnimationEnabled(True)
    page.addParam(afterParam)

    # -- Opacity (aliased to Multiply value, RGBA) --
    opacityParam = group.createColorParam("opacity", "Opacity", True)
    opacityParam.setDisplayMinimum(0, 0)
    opacityParam.setDisplayMaximum(1, 0)
    opacityParam.setDisplayMinimum(0, 1)
    opacityParam.setDisplayMaximum(1, 1)
    opacityParam.setDisplayMinimum(0, 2)
    opacityParam.setDisplayMaximum(1, 2)
    opacityParam.setDisplayMinimum(0, 3)
    opacityParam.setDisplayMaximum(1, 3)
    opacityParam.setDefaultValue(1, 0)
    opacityParam.setDefaultValue(1, 1)
    opacityParam.setDefaultValue(1, 2)
    opacityParam.setDefaultValue(1, 3)
    opacityParam.restoreDefaultValue(0)
    opacityParam.restoreDefaultValue(1)
    opacityParam.restoreDefaultValue(2)
    opacityParam.restoreDefaultValue(3)
    opacityParam.setAnimationEnabled(True)
    page.addParam(opacityParam)

    # -- Blending Mode (Choice, synced to Merge operation by C++) --
    blendingModeParam = group.createChoiceParam("blendingMode", "Blending Mode")
    blendingModeParam.addOption("atop", "")
    blendingModeParam.addOption("average", "")
    blendingModeParam.addOption("color", "")
    blendingModeParam.addOption("color-burn", "")
    blendingModeParam.addOption("color-dodge", "")
    blendingModeParam.addOption("conjoint-over", "")
    blendingModeParam.addOption("copy", "")
    blendingModeParam.addOption("difference", "")
    blendingModeParam.addOption("disjoint-over", "")
    blendingModeParam.addOption("divide", "")
    blendingModeParam.addOption("exclusion", "")
    blendingModeParam.addOption("freeze", "")
    blendingModeParam.addOption("from", "")
    blendingModeParam.addOption("geometric", "")
    blendingModeParam.addOption("grain-extract", "")
    blendingModeParam.addOption("grain-merge", "")
    blendingModeParam.addOption("hard-light", "")
    blendingModeParam.addOption("hue", "")
    blendingModeParam.addOption("hypot", "")
    blendingModeParam.addOption("in", "")
    blendingModeParam.addOption("luminosity", "")
    blendingModeParam.addOption("mask", "")
    blendingModeParam.addOption("matte", "")
    blendingModeParam.addOption("max", "")
    blendingModeParam.addOption("min", "")
    blendingModeParam.addOption("minus", "")
    blendingModeParam.addOption("multiply", "")
    blendingModeParam.addOption("out", "")
    blendingModeParam.addOption("over", "")
    blendingModeParam.addOption("overlay", "")
    blendingModeParam.addOption("pinlight", "")
    blendingModeParam.addOption("plus", "")
    blendingModeParam.addOption("reflect", "")
    blendingModeParam.addOption("saturation", "")
    blendingModeParam.addOption("screen", "")
    blendingModeParam.addOption("soft-light", "")
    blendingModeParam.addOption("stencil", "")
    blendingModeParam.addOption("under", "")
    blendingModeParam.addOption("xor", "")
    blendingModeParam.setDefaultValue(28)  # over
    blendingModeParam.restoreDefaultValue()
    blendingModeParam.setAnimationEnabled(True)
    page.addParam(blendingModeParam)

    # -- Transform: Translate --
    translateParam = group.createDouble2DParam("translate", "Translate")
    translateParam.setDisplayMinimum(-10000, 0)
    translateParam.setDisplayMaximum(10000, 0)
    translateParam.setDisplayMinimum(-10000, 1)
    translateParam.setDisplayMaximum(10000, 1)
    translateParam.setAnimationEnabled(True)
    page.addParam(translateParam)

    # -- Transform: Rotate --
    rotateParam = group.createDoubleParam("rotate", "Rotate")
    rotateParam.setDisplayMinimum(-180, 0)
    rotateParam.setDisplayMaximum(180, 0)
    rotateParam.setAnimationEnabled(True)
    page.addParam(rotateParam)

    # -- Transform: Scale --
    scaleParam = group.createDouble2DParam("scale", "Scale")
    scaleParam.setMinimum(-10000, 0)
    scaleParam.setMaximum(10000, 0)
    scaleParam.setDisplayMinimum(0.1, 0)
    scaleParam.setDisplayMaximum(10, 0)
    scaleParam.setDefaultValue(1, 0)
    scaleParam.restoreDefaultValue(0)
    scaleParam.setMinimum(-10000, 1)
    scaleParam.setMaximum(10000, 1)
    scaleParam.setDisplayMinimum(0.1, 1)
    scaleParam.setDisplayMaximum(10, 1)
    scaleParam.setDefaultValue(1, 1)
    scaleParam.restoreDefaultValue(1)
    scaleParam.setAnimationEnabled(True)
    page.addParam(scaleParam)

    # -- Transform: Uniform Scale --
    uniformParam = group.createBooleanParam("uniform", "Uniform Scale")
    uniformParam.setDefaultValue(False)
    uniformParam.restoreDefaultValue()
    page.addParam(uniformParam)

    # -- Transform: Skew X --
    skewXParam = group.createDoubleParam("skewX", "Skew X")
    skewXParam.setDisplayMinimum(-100, 0)
    skewXParam.setDisplayMaximum(100, 0)
    skewXParam.setAnimationEnabled(True)
    page.addParam(skewXParam)

    # -- Transform: Skew Y --
    skewYParam = group.createDoubleParam("skewY", "Skew Y")
    skewYParam.setDisplayMinimum(-100, 0)
    skewYParam.setDisplayMaximum(100, 0)
    skewYParam.setAnimationEnabled(True)
    page.addParam(skewYParam)

    # -- Transform: Skew Order --
    skewOrderParam = group.createChoiceParam("skewOrder", "Skew Order")
    skewOrderParam.setDefaultValue(0)
    page.addParam(skewOrderParam)

    # -- Transform: Center --
    centerParam = group.createDouble2DParam("center", "Center")
    centerParam.setDisplayMinimum(-10000, 0)
    centerParam.setDisplayMaximum(10000, 0)
    centerParam.setDisplayMinimum(-10000, 1)
    centerParam.setDisplayMaximum(10000, 1)
    centerParam.setAnimationEnabled(True)
    page.addParam(centerParam)

    # -- Transform: Motion Blur --
    motionBlurParam = group.createDoubleParam("motionBlur", "Motion Blur")
    motionBlurParam.setMinimum(0, 0)
    motionBlurParam.setMaximum(100, 0)
    motionBlurParam.setDisplayMinimum(0, 0)
    motionBlurParam.setDisplayMaximum(4, 0)
    motionBlurParam.setAnimationEnabled(True)
    page.addParam(motionBlurParam)

    # -- Transform: Shutter --
    shutterParam = group.createDoubleParam("shutter", "Shutter")
    shutterParam.setMinimum(0, 0)
    shutterParam.setMaximum(2, 0)
    shutterParam.setDisplayMinimum(0, 0)
    shutterParam.setDisplayMaximum(2, 0)
    shutterParam.setDefaultValue(0.5, 0)
    shutterParam.restoreDefaultValue(0)
    shutterParam.setAnimationEnabled(True)
    page.addParam(shutterParam)

    # -- Transform: Interactive Update --
    interactiveParam = group.createBooleanParam("interactive", "Interactive Update")
    interactiveParam.setDefaultValue(True)
    interactiveParam.restoreDefaultValue()
    page.addParam(interactiveParam)

    group.setPagesOrder(["LayerSettings", "Node", "Settings"])
    group.refreshUserParamsGUI()

    # ====================================================================
    # 2. Create internal nodes (no Read — that's external now)
    # ====================================================================

    # Input node — receives the external Read node's output
    inputNode = app.createNode("fr.inria.built-in.Input", 1, group)
    inputNode.setLabel("Input1")
    inputNode.setPosition(0, -200)
    inputNode.setSize(82, 33)

    # FrameRange node
    frameRangeNode = app.createNode("net.sf.openfx.FrameRange", 1, group)
    frameRangeNode.setScriptName("FrameRange1")
    frameRangeNode.setLabel("FrameRange1")
    frameRangeNode.setPosition(0, -100)
    frameRangeNode.setSize(82, 56)

    # TimeOffset node
    timeOffsetNode = app.createNode("net.sf.openfx.timeOffset", 1, group)
    timeOffsetNode.setScriptName("TimeOffset1")
    timeOffsetNode.setLabel("TimeOffset1")
    timeOffsetNode.setPosition(0, 0)
    timeOffsetNode.setSize(82, 33)

    # Transform node
    transformNode = app.createNode("net.sf.openfx.TransformPlugin", 1, group)
    transformNode.setScriptName("Transform1")
    transformNode.setLabel("Transform1")
    transformNode.setPosition(0, 100)
    transformNode.setSize(82, 33)

    # Multiply node (opacity via value RGBA)
    multiplyNode = app.createNode("net.sf.openfx.MultiplyPlugin", 1, group)
    multiplyNode.setScriptName("Multiply1")
    multiplyNode.setLabel("Multiply1")
    multiplyNode.setPosition(0, 200)
    multiplyNode.setSize(82, 33)
    multiplyNode.getParam("NatronOfxParamProcessR").setValue(True)
    multiplyNode.getParam("NatronOfxParamProcessG").setValue(True)
    multiplyNode.getParam("NatronOfxParamProcessB").setValue(True)
    multiplyNode.getParam("NatronOfxParamProcessA").setValue(True)

    # Output node
    outputNode = app.createNode("fr.inria.built-in.Output", 1, group)
    outputNode.setLabel("Output1")
    outputNode.setPosition(0, 300)
    outputNode.setSize(106, 33)

    # ====================================================================
    # 3. Connect: Input -> FrameRange -> TimeOffset -> Transform -> Multiply -> Output
    # ====================================================================

    frameRangeNode.connectInput(0, inputNode)
    timeOffsetNode.connectInput(0, frameRangeNode)
    transformNode.connectInput(0, timeOffsetNode)
    multiplyNode.connectInput(0, transformNode)
    outputNode.connectInput(0, multiplyNode)

    # ====================================================================
    # 4. Alias group params to internal node params
    # ====================================================================

    # FrameRange
    group.getParam("frameRange").setAsAlias(frameRangeNode.getParam("frameRange"))

    # TimeOffset
    group.getParam("timeOffset").setAsAlias(timeOffsetNode.getParam("timeOffset"))

    # Before/After
    group.getParam("before").setAsAlias(frameRangeNode.getParam("before"))
    group.getParam("after").setAsAlias(frameRangeNode.getParam("after"))

    # Opacity
    group.getParam("opacity").setAsAlias(multiplyNode.getParam("value"))

    # Transform params
    group.getParam("translate").setAsAlias(transformNode.getParam("translate"))
    group.getParam("rotate").setAsAlias(transformNode.getParam("rotate"))
    group.getParam("scale").setAsAlias(transformNode.getParam("scale"))
    group.getParam("uniform").setAsAlias(transformNode.getParam("uniform"))
    group.getParam("skewX").setAsAlias(transformNode.getParam("skewX"))
    group.getParam("skewY").setAsAlias(transformNode.getParam("skewY"))
    group.getParam("skewOrder").setAsAlias(transformNode.getParam("skewOrder"))
    group.getParam("center").setAsAlias(transformNode.getParam("center"))
    group.getParam("motionBlur").setAsAlias(transformNode.getParam("motionBlur"))
    group.getParam("shutter").setAsAlias(transformNode.getParam("shutter"))
    group.getParam("interactive").setAsAlias(transformNode.getParam("interactive"))
