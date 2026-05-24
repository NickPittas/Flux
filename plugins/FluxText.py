# -*- coding: utf-8 -*-
# FluxText PyPlug — Text -> FrameRange -> TimeOffset -> Grade -> Output
# Text controls are promoted group knobs, matching Natron's PyPlug exporter style.

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
    return "Flux text layer: Text -> FrameRange -> TimeOffset -> Grade -> Output"

def _set_default(param, values):
    for dim, value in enumerate(values):
        param.setDefaultValue(value, dim)
        param.restoreDefaultValue(dim)

def _set_range(param, ranges):
    for dim, values in enumerate(ranges):
        if "min" in values:
            param.setMinimum(values["min"], dim)
        if "max" in values:
            param.setMaximum(values["max"], dim)
        if "displayMin" in values:
            param.setDisplayMinimum(values["displayMin"], dim)
        if "displayMax" in values:
            param.setDisplayMaximum(values["displayMax"], dim)

def _finalize(page, owner, param, attr_name, add_new_line=True, anim=None, eval_on_change=None, value=None):
    page.addParam(param)
    param.setAddNewLine(add_new_line)
    if eval_on_change is not None:
        param.setEvaluateOnChange(eval_on_change)
    if anim is not None:
        param.setAnimationEnabled(anim)
    if value is not None:
        if isinstance(value, (tuple, list)):
            for dim, v in enumerate(value):
                param.setValue(v, dim)
        else:
            param.setValue(value)
    setattr(owner, attr_name, param)

def _create_text_promoted_param(group, page, spec):
    name = "Text1" + spec["name"]
    kind = spec["kind"]
    label = spec["label"]
    if kind == "double":
        param = group.createDoubleParam(name, label)
    elif kind == "double2d":
        param = group.createDouble2DParam(name, label)
    elif kind == "double3d":
        param = group.createDouble3DParam(name, label)
    elif kind == "int":
        param = group.createIntParam(name, label)
    elif kind == "int2d":
        param = group.createInt2DParam(name, label)
    elif kind == "bool":
        param = group.createBooleanParam(name, label)
    elif kind == "choice":
        param = group.createChoiceParam(name, label)
    elif kind == "file":
        param = group.createFileParam(name, label)
        param.setSequenceEnabled(False)
    elif kind == "string":
        param = group.createStringParam(name, label)
        string_type = spec.get("stringType")
        if string_type == "multiline":
            param.setType(NatronEngine.StringParam.TypeEnum.eStringTypeMultiLine)
        elif string_type == "default":
            param.setType(NatronEngine.StringParam.TypeEnum.eStringTypeDefault)
    elif kind == "color":
        param = group.createColorParam(name, label, True)
    else:
        raise RuntimeError("Unsupported FluxText promoted kind: %s" % kind)

    if "range" in spec:
        _set_range(param, spec["range"])
    if "default" in spec:
        default = spec["default"]
        if isinstance(default, (tuple, list)):
            _set_default(param, default)
        else:
            param.setDefaultValue(default)
            param.restoreDefaultValue()
    _finalize(page, group, param, name,
              anim=spec.get("anim", False),
              eval_on_change=spec.get("eval"),
              value=spec.get("value"))

def _add_flux_contract_params(group, page):
    frameRangeParam = group.createInt2DParam("frameRange", "Frame Range")
    frameRangeParam.setDefaultValue(1, 0)
    frameRangeParam.setDefaultValue(100, 1)
    _finalize(page, group, frameRangeParam, "frameRange")

    timeOffsetParam = group.createIntParam("timeOffset", "Time Offset (Frames)")
    timeOffsetParam.setDefaultValue(0, 0)
    _finalize(page, group, timeOffsetParam, "timeOffset")

    beforeParam = group.createChoiceParam("before", "Before Behavior")
    for option in ["original", "hold", "black", "loop", "bounce"]:
        beforeParam.addOption(option, "")
    beforeParam.setDefaultValue(2)
    beforeParam.restoreDefaultValue()
    _finalize(page, group, beforeParam, "before", anim=True)

    afterParam = group.createChoiceParam("after", "After Behavior")
    for option in ["original", "hold", "black", "loop", "bounce"]:
        afterParam.addOption(option, "")
    afterParam.setDefaultValue(2)
    afterParam.restoreDefaultValue()
    _finalize(page, group, afterParam, "after", anim=True)

    opacityParam = group.createColorParam("opacity", "Opacity", True)
    _set_range(opacityParam, [{"displayMin": 0, "displayMax": 1}] * 4)
    _set_default(opacityParam, (1, 1, 1, 1))
    _finalize(page, group, opacityParam, "opacity", anim=True)

    blendingModeParam = group.createChoiceParam("blendingMode", "Blending Mode")
    for option in [
        "atop", "average", "color", "color-burn", "color-dodge", "conjoint-over",
        "copy", "difference", "disjoint-over", "divide", "exclusion", "freeze",
        "from", "geometric", "grain-extract", "grain-merge", "hard-light", "hue",
        "hypot", "in", "luminosity", "mask", "matte", "max", "min", "minus",
        "multiply", "out", "over", "overlay", "pinlight", "plus", "reflect",
        "saturation", "screen", "soft-light", "stencil", "under", "xor"
    ]:
        blendingModeParam.addOption(option, "")
    blendingModeParam.setDefaultValue(28)
    blendingModeParam.restoreDefaultValue()
    _finalize(page, group, blendingModeParam, "blendingMode", anim=True)

    overlayCenterParam = group.createDouble2DParam("textOverlayCenter", "Text Overlay Center")
    overlayCenterParam.setDefaultValue(0, 0)
    overlayCenterParam.restoreDefaultValue(0)
    overlayCenterParam.setDefaultValue(0, 1)
    overlayCenterParam.restoreDefaultValue(1)
    overlayCenterParam.setVisibleByDefault(False)
    _finalize(page, group, overlayCenterParam, "textOverlayCenter", anim=False)

TEXT_PARAM_SPECS = [
    {"name": "rotate", "label": "Rotate", "kind": "double", "range": [{"displayMin": -180, "displayMax": 180}], "anim": True},
    {"name": "scale", "label": "Scale", "kind": "double2d", "range": [{"min": -10000, "max": 10000, "displayMin": 0.1, "displayMax": 10}, {"min": -10000, "max": 10000, "displayMin": 0.1, "displayMax": 10}], "default": (1, 1), "anim": True},
    {"name": "uniform", "label": "Uniform", "kind": "bool", "anim": True},
    {"name": "skewX", "label": "Skew X", "kind": "double", "range": [{"displayMin": -1, "displayMax": 1}], "anim": True},
    {"name": "skewY", "label": "Skew Y", "kind": "double", "range": [{"displayMin": -1, "displayMax": 1}], "anim": True},
    {"name": "skewOrder", "label": "Skew Order", "kind": "choice", "anim": True},
    {"name": "transformAmount", "label": "Amount", "kind": "double", "range": [{"displayMin": 0, "displayMax": 1}], "default": 1, "anim": True},
    {"name": "center", "label": "Center", "kind": "double2d", "range": [{"displayMin": -10000, "displayMax": 10000}, {"displayMin": -10000, "displayMax": 10000}], "default": (0.5, 0.5), "value": (890, 540), "anim": True},
    {"name": "interactive", "label": "Interactive Update", "kind": "bool", "default": True, "eval": False, "anim": False},
    {"name": "hidpi", "label": "HiDPI", "kind": "bool", "value": True, "eval": False, "anim": False},
    {"name": "transform", "label": "Transform", "kind": "bool", "default": True, "anim": False},
    {"name": "autoSize", "label": "Auto size", "kind": "bool", "anim": False},
    {"name": "centerInteract", "label": "Center Interact", "kind": "bool", "anim": False},
    {"name": "canvas", "label": "Canvas size", "kind": "int2d", "range": [{"min": 0, "max": 10000, "displayMin": 0, "displayMax": 4000}, {"min": 0, "max": 10000, "displayMin": 0, "displayMax": 4000}], "default": (0, 0), "anim": False},
    {"name": "markup", "label": "Markup", "kind": "bool", "anim": False},
    {"name": "file", "label": "Text File", "kind": "file", "anim": False},
    {"name": "subtitle", "label": "Subtitle File", "kind": "file", "anim": False},
    {"name": "fps", "label": "Frame Rate", "kind": "double", "range": [{"min": -1.79769e+308, "max": 1.79769e+308, "displayMin": -1.79769e+308, "displayMax": 1.79769e+308}], "default": 24, "anim": False},
    {"name": "text", "label": "Text", "kind": "string", "stringType": "multiline", "default": "Text", "anim": True},
    {"name": "justify", "label": "Justify", "kind": "bool", "anim": False},
    {"name": "wrap", "label": "Wrap", "kind": "choice", "anim": False},
    {"name": "align", "label": "Horizontal align", "kind": "choice", "anim": False},
    {"name": "valign", "label": "Vertical align", "kind": "choice", "anim": False},
    {"name": "name", "label": "Select font", "kind": "choice", "default": 11, "anim": False},
    {"name": "custom", "label": "Custom font(s)", "kind": "file", "anim": False},
    {"name": "font", "label": "Font family", "kind": "string", "stringType": "default", "default": "Arial", "anim": True},
    {"name": "size", "label": "Font size", "kind": "int", "range": [{"min": 1, "max": 10000, "displayMin": 1, "displayMax": 500}], "default": 64, "value": 84, "anim": True},
    {"name": "color", "label": "Font color", "kind": "color", "range": [{"min": -1.79769e+308, "max": 1.79769e+308, "displayMin": 0, "displayMax": 1}] * 4, "default": (1, 1, 1, 1), "anim": True},
    {"name": "backgroundColor", "label": "Background Color", "kind": "color", "range": [{"min": -1.79769e+308, "max": 1.79769e+308, "displayMin": 0, "displayMax": 1}] * 4, "anim": True},
    {"name": "letterSpace", "label": "Letter spacing", "kind": "int", "range": [{"min": -10000, "max": 10000, "displayMin": -250, "displayMax": 250}], "default": 0, "anim": True},
    {"name": "hintStyle", "label": "Hint style", "kind": "choice", "anim": False},
    {"name": "hintMetrics", "label": "Hint metrics", "kind": "choice", "anim": False},
    {"name": "antialiasing", "label": "Antialiasing", "kind": "choice", "anim": False},
    {"name": "subpixel", "label": "Subpixel", "kind": "choice", "anim": False},
    {"name": "style", "label": "Style", "kind": "choice", "anim": False},
    {"name": "weight", "label": "Weight", "kind": "choice", "default": 5, "anim": False},
    {"name": "stretch", "label": "Stretch", "kind": "choice", "default": 4, "anim": False},
    {"name": "strokeSize", "label": "Stroke size", "kind": "double", "range": [{"min": 0, "max": 500, "displayMin": 0, "displayMax": 100}], "anim": True},
    {"name": "strokeColor", "label": "Stroke color", "kind": "color", "range": [{"min": -1.79769e+308, "max": 1.79769e+308, "displayMin": 0, "displayMax": 1}] * 4, "default": (1, 0, 0, 1), "anim": True},
    {"name": "strokeDash", "label": "Stroke dash length", "kind": "int", "range": [{"min": 0, "max": 100, "displayMin": 0, "displayMax": 10}], "default": 0, "anim": True},
    {"name": "strokeDashPattern", "label": "Stroke dash pattern", "kind": "double3d", "range": [{"min": -1.79769e+308, "max": 1.79769e+308, "displayMin": -1.79769e+308, "displayMax": 1.79769e+308}] * 3, "default": (1, 0, 0), "anim": True},
    {"name": "circleRadius", "label": "Circle radius", "kind": "double", "range": [{"min": 0, "max": 10000, "displayMin": 0, "displayMax": 1000}], "anim": True},
    {"name": "circleWords", "label": "Circle Words", "kind": "int", "range": [{"min": 1, "max": 1000, "displayMin": 1, "displayMax": 100}], "default": 10, "anim": True},
    {"name": "arcRadius", "label": "Arc Radius", "kind": "double", "range": [{"min": 0, "max": 10000, "displayMin": 0, "displayMax": 1000}], "default": 100, "anim": True},
    {"name": "arcAngle", "label": "Arc Angle", "kind": "double", "range": [{"min": 0, "max": 360, "displayMin": 0, "displayMax": 360}], "anim": True},
    {"name": "scrollX", "label": "Scroll X", "kind": "double", "range": [{"min": -10000, "max": 10000, "displayMin": -4000, "displayMax": 4000}], "anim": True},
    {"name": "scrollY", "label": "Scroll Y", "kind": "double", "range": [{"min": -10000, "max": 10000, "displayMin": -4000, "displayMax": 4000}], "anim": True},
    {"name": "channels", "label": "Output Layer", "kind": "choice", "anim": False},
    {"name": "processAllPlanes", "label": "All Planes", "kind": "bool", "default": True, "anim": False},
]

def createInstance(app, group):
    group.setColor(0.7, 0.7, 0.7)
    fluxPage = group.createPageParam("FluxTextSettings", "Flux")
    _add_flux_contract_params(group, fluxPage)

    textPage = group.createPageParam("userNatron", "Text")
    for spec in TEXT_PARAM_SPECS:
        _create_text_promoted_param(group, textPage, spec)

    group.setPagesOrder(["userNatron", "FluxTextSettings", "Node", "Settings"])
    group.refreshUserParamsGUI()

    textNode = app.createNode("net.fxarena.openfx.Text", 6, group)
    if textNode is None:
        raise RuntimeError("Failed to create Text1")
    textNode.setScriptName("Text1")
    textNode.setLabel("Text1")
    textNode.setPosition(0, -200)
    textNode.setColor(0.3, 0.5, 0.2)
    for name, value in [("transform", True), ("hidpi", True), ("autoSize", False), ("markup", False), ("text", "Text"), ("size", 84)]:
        param = textNode.getParam(name)
        if param is not None:
            if isinstance(value, bool) or isinstance(value, str):
                param.setValue(value)
            else:
                param.setValue(value, 0)

    frameRangeNode = app.createNode("net.sf.openfx.FrameRange", 1, group)
    if frameRangeNode is None:
        raise RuntimeError("Failed to create FrameRange1")
    frameRangeNode.setScriptName("FrameRange1")
    frameRangeNode.setLabel("FrameRange1")
    frameRangeNode.setPosition(0, -100)
    frameRangeNode.setSize(82, 56)

    timeOffsetNode = app.createNode("net.sf.openfx.timeOffset", 1, group)
    if timeOffsetNode is None:
        raise RuntimeError("Failed to create TimeOffset1")
    timeOffsetNode.setScriptName("TimeOffset1")
    timeOffsetNode.setLabel("TimeOffset1")
    timeOffsetNode.setPosition(0, 0)

    gradeNode = app.createNode("net.sf.openfx.GradePlugin", 2, group)
    if gradeNode is None:
        raise RuntimeError("Failed to create Grade1")
    gradeNode.setScriptName("Grade1")
    gradeNode.setLabel("Grade1")
    gradeNode.setPosition(0, 100)

    outputNode = app.createNode("fr.inria.built-in.Output", 1, group)
    if outputNode is None:
        raise RuntimeError("Failed to create Output1")
    outputNode.setLabel("Output1")
    outputNode.setPosition(0, 200)

    frameRangeNode.connectInput(0, textNode)
    timeOffsetNode.connectInput(0, frameRangeNode)
    gradeNode.connectInput(0, timeOffsetNode)
    outputNode.connectInput(0, gradeNode)

    group.getParam("frameRange").setAsAlias(frameRangeNode.getParam("frameRange"))
    group.getParam("timeOffset").setAsAlias(timeOffsetNode.getParam("timeOffset"))
    group.getParam("before").setAsAlias(frameRangeNode.getParam("before"))
    group.getParam("after").setAsAlias(frameRangeNode.getParam("after"))
    group.getParam("opacity").setAsAlias(gradeNode.getParam("multiply"))

    for spec in TEXT_PARAM_SPECS:
        source = textNode.getParam(spec["name"])
        promoted = group.getParam("Text1" + spec["name"])
        if source is not None and promoted is not None:
            promoted.setAsAlias(source)
