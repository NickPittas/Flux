# -*- coding: utf-8 -*-
# FluxText PyPlug — wraps native Text OFX knobs front-and-center.
# Pipeline: Text → TimeOffset → Multiply → Output
#
# Important contract with C++:
#   frameRange is aliased directly to the native Text node frameRange knob.
#   Text controls are aliases to the actual native Text node knobs dumped from
#   net.fxarena.openfx.Text. They are required unless explicitly internal.

import NatronEngine

def getPluginID():
    return "net.sf.openfx.FluxText"

def getLabel():
    return "FluxText"

def getVersion():
    return 1

def getIconPath():
    return ""

def getGrouping():
    return "Flux"

def getDescription():
    return "Flux text layer: native Text -> TimeOffset -> Multiply -> Output"

def _add_string(group, page, name, label, default=""):
    param = group.createStringParam(name, label)
    if default:
        param.setDefaultValue(default)
        param.restoreDefaultValue()
    param.setAnimationEnabled(True)
    page.addParam(param)

def _add_double(group, page, name, label, default=None):
    param = group.createDoubleParam(name, label)
    if default is not None:
        param.setDefaultValue(default, 0)
        param.restoreDefaultValue(0)
    param.setAnimationEnabled(True)
    page.addParam(param)

def _add_int(group, page, name, label, default=None):
    param = group.createIntParam(name, label)
    if default is not None:
        param.setDefaultValue(default, 0)
        param.restoreDefaultValue(0)
    param.setAnimationEnabled(True)
    page.addParam(param)

def _add_int2d(group, page, name, label, default_x=None, default_y=None):
    param = group.createInt2DParam(name, label)
    if default_x is not None:
        param.setDefaultValue(default_x, 0)
        param.restoreDefaultValue(0)
    if default_y is not None:
        param.setDefaultValue(default_y, 1)
        param.restoreDefaultValue(1)
    param.setAnimationEnabled(True)
    page.addParam(param)

def _add_double2d(group, page, name, label, default_x=None, default_y=None):
    param = group.createDouble2DParam(name, label)
    if default_x is not None:
        param.setDefaultValue(default_x, 0)
        param.restoreDefaultValue(0)
    if default_y is not None:
        param.setDefaultValue(default_y, 1)
        param.restoreDefaultValue(1)
    param.setAnimationEnabled(True)
    page.addParam(param)

def _add_bool(group, page, name, label, default=False):
    param = group.createBooleanParam(name, label)
    param.setDefaultValue(default)
    param.restoreDefaultValue()
    param.setAnimationEnabled(True)
    page.addParam(param)

def _add_color(group, page, name, label, default=(0, 0, 0, 0)):
    param = group.createColorParam(name, label, True)
    for i, value in enumerate(default):
        param.setDisplayMinimum(0, i)
        param.setDisplayMaximum(1, i)
        param.setDefaultValue(value, i)
        param.restoreDefaultValue(i)
    param.setAnimationEnabled(True)
    page.addParam(param)

def _add_choice(group, page, name, label, options, default=0):
    param = group.createChoiceParam(name, label)
    for option in options:
        param.addOption(option, "")
    param.setDefaultValue(default)
    param.restoreDefaultValue()
    param.setAnimationEnabled(True)
    page.addParam(param)

def _alias(group, group_name, node, node_name, required=True):
    source = group.getParam(group_name)
    target = node.getParam(node_name) if node else None
    if source is None or target is None:
        if required:
            raise RuntimeError("FluxText alias failed: %s -> %s" % (group_name, node_name))
        return
    source.setAsAlias(target)

def createInstance(app, group):
    page = group.createPageParam("TextSettings", "Text Settings")

    # Native Text OFX content/style knobs. Keep these first: text work lives here.
    _add_bool(group, page, "autoSize", "Auto Size", True)
    _add_bool(group, page, "centerInteract", "Center Interact", False)
    _add_int2d(group, page, "canvas", "Canvas Size", 0, 0)
    _add_string(group, page, "text", "Text", "Text")
    _add_double(group, page, "fps", "Frame Rate", 24)
    _add_string(group, page, "font", "Font", "D/DejaVu Sans")
    _add_choice(group, page, "name", "Select Font", ["Default"], 0)
    _add_int(group, page, "size", "Size", 64)
    _add_color(group, page, "color", "Font Color", (1, 1, 1, 1))
    _add_choice(group, page, "weight", "Weight", [
        "Thin", "Ultralight", "Light", "Book", "Normal", "Medium",
        "Semibold", "Bold", "Ultrabold", "Heavy", "Ultraheavy"
    ], 4)
    _add_choice(group, page, "stretch", "Stretch", [
        "UltraCondensed", "ExtraCondensed", "Condensed", "SemiCondensed",
        "Normal", "SemiExpanded", "Expanded", "ExtraExpanded", "UltraExpanded"
    ], 4)
    _add_choice(group, page, "style", "Style", ["Normal", "Italic", "Oblique"], 0)
    _add_bool(group, page, "markup", "Pango Markup", True)
    _add_bool(group, page, "justify", "Justify", False)
    _add_choice(group, page, "wrap", "Wrap", ["None", "Word", "Char", "WordChar"], 0)
    _add_choice(group, page, "align", "Horizontal Align", ["Left", "Center", "Right"], 0)
    _add_choice(group, page, "valign", "Vertical Align", ["Top", "Center", "Bottom"], 0)
    _add_double2d(group, page, "textCenter", "Text Position")
    _add_string(group, page, "custom", "Custom Font")
    _add_string(group, page, "file", "Text File")
    _add_string(group, page, "subtitle", "Subtitle File")
    _add_int(group, page, "letterSpace", "Letter Spacing", 0)
    _add_double(group, page, "scrollX", "Scroll X", 0)
    _add_double(group, page, "scrollY", "Scroll Y", 0)
    _add_color(group, page, "backgroundColor", "Background Color", (0, 0, 0, 0))
    _add_color(group, page, "strokeColor", "Stroke Color", (1, 1, 1, 1))
    _add_double(group, page, "strokeSize", "Stroke Size", 0)
    _add_int(group, page, "strokeDash", "Stroke Dash Length", 0)
    _add_double(group, page, "strokeDashPattern", "Stroke Dash Pattern", 1)
    _add_choice(group, page, "hintStyle", "Hint Style", ["Default"], 0)
    _add_choice(group, page, "hintMetrics", "Hint Metrics", ["Default"], 0)
    _add_choice(group, page, "antialiasing", "Antialiasing", ["None", "Gray", "Subpixel", "Default"], 3)
    _add_choice(group, page, "subpixel", "Subpixel", ["Default"], 0)
    _add_double(group, page, "circleRadius", "Circle Radius", 0)
    _add_int(group, page, "circleWords", "Circle Words", 10)
    _add_double(group, page, "arcAngle", "Arc Angle", 0)
    _add_double(group, page, "arcRadius", "Arc Radius", 100)

    # Native Text transform knobs, aliased to Flux's stable transform names.
    _add_bool(group, page, "transform", "Enable Text Transform", True)
    _add_double(group, page, "transformAmount", "Transform Amount", 1)
    _add_double2d(group, page, "translate", "Translate")
    _add_double(group, page, "rotate", "Rotate", 0)
    _add_double2d(group, page, "scale", "Scale", 1, 1)
    _add_bool(group, page, "uniform", "Uniform Scale", False)
    _add_double(group, page, "skewX", "Skew X", 0)
    _add_double(group, page, "skewY", "Skew Y", 0)
    _add_choice(group, page, "skewOrder", "Skew Order", ["XY", "YX"], 0)
    _add_double2d(group, page, "center", "Transform Center")
    _add_bool(group, page, "transformCenterChanged", "Transform Center Changed", False)
    _add_bool(group, page, "transformInteractOpen", "Show Interact", False)
    _add_bool(group, page, "interactive", "Interactive Update", True)
    _add_bool(group, page, "hidpi", "HiDPI", False)

    # Flux timeline/compositing contract.
    frameRangeParam = group.createInt2DParam("frameRange", "Frame Range")
    frameRangeParam.setDefaultValue(1, 0)
    frameRangeParam.setDefaultValue(100, 1)
    page.addParam(frameRangeParam)
    enableNodeLifeTimeParam = group.createBooleanParam("enableNodeLifeTime", "Enable Lifetime")
    enableNodeLifeTimeParam.setDefaultValue(True)
    enableNodeLifeTimeParam.restoreDefaultValue()
    page.addParam(enableNodeLifeTimeParam)
    nodeLifeTimeParam = group.createInt2DParam("nodeLifeTime", "Lifetime Range")
    nodeLifeTimeParam.setDefaultValue(1, 0)
    nodeLifeTimeParam.setDefaultValue(100, 1)
    page.addParam(nodeLifeTimeParam)
    timeOffsetParam = group.createIntParam("timeOffset", "Time Offset (Frames)")
    timeOffsetParam.setDefaultValue(0, 0)
    page.addParam(timeOffsetParam)
    _add_color(group, page, "opacity", "Opacity", (1, 1, 1, 1))
    _add_choice(group, page, "blendingMode", "Blending Mode", [
        "atop", "average", "color", "color-burn", "color-dodge", "conjoint-over",
        "copy", "difference", "disjoint-over", "divide", "exclusion", "freeze",
        "from", "geometric", "grain-extract", "grain-merge", "hard-light", "hue",
        "hypot", "in", "luminosity", "mask", "matte", "max", "min", "minus",
        "multiply", "out", "over", "overlay", "pinlight", "plus", "reflect",
        "saturation", "screen", "soft-light", "stencil", "under", "xor"
    ], 28)

    group.setPagesOrder(["TextSettings", "Node", "Settings"])
    group.refreshUserParamsGUI()

    textNode = app.createNode("net.fxarena.openfx.Text", 6, group)
    textNode.setScriptName("Text1")
    textNode.setLabel("Text1")
    textNode.setPosition(0, -200)
    textNode.setSize(150, 100)

    # Native Text transform is the layer transform. There is no extra Transform node.
    param = textNode.getParam("transform")
    if param is not None:
        param.setValue(True)
    param = textNode.getParam("autoSize")
    if param is not None:
        param.setValue(True)
    param = textNode.getParam("markup")
    if param is not None:
        param.setValue(True)
    param = textNode.getParam("text")
    if param is not None:
        param.setValue("Text")
    param = textNode.getParam("font")
    if param is not None:
        param.setValue("D/DejaVu Sans")
    param = textNode.getParam("size")
    if param is not None:
        param.setValue(64, 0)

    timeOffsetNode = app.createNode("net.sf.openfx.timeOffset", 1, group)
    timeOffsetNode.setScriptName("TimeOffset1")
    timeOffsetNode.setLabel("TimeOffset1")
    timeOffsetNode.setPosition(0, -60)
    timeOffsetNode.setSize(82, 33)

    multiplyNode = app.createNode("net.sf.openfx.MultiplyPlugin", 1, group)
    multiplyNode.setScriptName("Multiply1")
    multiplyNode.setLabel("Multiply1")
    multiplyNode.setPosition(0, 20)
    multiplyNode.setSize(82, 33)
    multiplyNode.getParam("NatronOfxParamProcessR").setValue(True)
    multiplyNode.getParam("NatronOfxParamProcessG").setValue(True)
    multiplyNode.getParam("NatronOfxParamProcessB").setValue(True)
    multiplyNode.getParam("NatronOfxParamProcessA").setValue(True)

    outputNode = app.createNode("fr.inria.built-in.Output", 1, group)
    outputNode.setLabel("Output1")
    outputNode.setPosition(0, 100)
    outputNode.setSize(106, 33)

    timeOffsetNode.connectInput(0, textNode)
    multiplyNode.connectInput(0, timeOffsetNode)
    outputNode.connectInput(0, multiplyNode)

    for name in [
        "rotate", "scale", "uniform", "skewX", "skewY", "skewOrder", "transformAmount",
        "transformCenterChanged", "transformInteractOpen", "interactive", "hidpi", "transform",
        "autoSize", "centerInteract", "canvas", "markup", "file", "subtitle", "fps", "text",
        "justify", "wrap", "align", "valign", "name", "custom", "font", "size", "color",
        "backgroundColor", "letterSpace", "hintStyle", "hintMetrics", "antialiasing",
        "subpixel", "style", "weight", "stretch", "strokeSize", "strokeColor", "strokeDash",
        "strokeDashPattern", "circleRadius", "circleWords", "arcRadius", "arcAngle", "scrollX",
        "scrollY", "frameRange", "enableNodeLifeTime", "nodeLifeTime"
    ]:
        _alias(group, name, textNode, name)

    _alias(group, "textCenter", textNode, "center")
    # The FxArena Text node exposes its transform knobs unprefixed.
    # Its Center knob is the onscreen text/transform position; do not set it
    # from C++ just to place the default text. Let Text's own defaults and
    # text-alignment knobs define the text's internal anchor/bounds behavior.
    _alias(group, "translate", textNode, "center")
    _alias(group, "rotate", textNode, "rotate")
    _alias(group, "scale", textNode, "scale")
    _alias(group, "uniform", textNode, "uniform")
    _alias(group, "skewX", textNode, "skewX")
    _alias(group, "skewY", textNode, "skewY")
    _alias(group, "skewOrder", textNode, "skewOrder")
    _alias(group, "center", textNode, "center")
    _alias(group, "interactive", textNode, "interactive")
    _alias(group, "timeOffset", timeOffsetNode, "timeOffset")
    _alias(group, "opacity", multiplyNode, "value")
