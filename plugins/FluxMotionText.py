# -*- coding: utf-8 -*-
# FluxMotionText PyPlug — Flux-owned motion text layer.
# Internal graph: TextRender -> FrameRange -> TimeOffset -> Transform -> Grade -> Output.
#
# This is separate from legacy FluxText.py. It intentionally stores text
# animators as dynamic user params added at runtime; there are no fixed
# animator slots here.

import NatronEngine


def getPluginID():
    return "net.sf.openfx.FluxMotionText"


def getLabel():
    return "FluxMotionText"


def getVersion():
    return 1


def getIconPath():
    return ""


def getGrouping():
    return "Flux"


def getDescription():
    return "Flux motion text layer: TextRender -> FrameRange -> TimeOffset -> Transform -> Grade -> Output"


def _set_default(param, values):
    if isinstance(values, (tuple, list)):
        for dim, value in enumerate(values):
            param.setDefaultValue(value, dim)
            param.restoreDefaultValue(dim)
    else:
        param.setDefaultValue(values)
        param.restoreDefaultValue()


def _set_display_range(param, minimum, maximum, dims):
    for dim in range(dims):
        param.setDisplayMinimum(minimum, dim)
        param.setDisplayMaximum(maximum, dim)


def _set_scalar_range(param, minimum, maximum, display_minimum, display_maximum):
    param.setMinimum(minimum, 0)
    param.setMaximum(maximum, 0)
    param.setDisplayMinimum(display_minimum, 0)
    param.setDisplayMaximum(display_maximum, 0)


def _finalize(page, owner, param, attr_name, anim=False, hidden=False):
    if anim:
        param.setAnimationEnabled(True)
    if hidden:
        param.setVisibleByDefault(False)
    page.addParam(param)
    param.setPersistent(True)
    setattr(owner, attr_name, param)


def _add_motion_text_params(group, page):
    text_param = group.createStringParam("text", "Text")
    text_param.setType(NatronEngine.StringParam.TypeEnum.eStringTypeMultiLine)
    _set_default(text_param, "Flux Motion Text")
    _finalize(page, group, text_param, "text", anim=True)

    font_param = group.createStringParam("font", "Font Family")
    font_param.setType(NatronEngine.StringParam.TypeEnum.eStringTypeDefault)
    _set_default(font_param, "Sans")
    _finalize(page, group, font_param, "font", anim=True)

    font_style_param = group.createStringParam("fontStyle", "Font Style")
    font_style_param.setType(NatronEngine.StringParam.TypeEnum.eStringTypeDefault)
    _set_default(font_style_param, "Regular")
    _finalize(page, group, font_style_param, "fontStyle", anim=True)

    font_size_param = group.createDoubleParam("fontSize", "Font Size")
    _set_scalar_range(font_size_param, 1, 2000, 1, 300)
    _set_default(font_size_param, 96)
    _finalize(page, group, font_size_param, "fontSize", anim=True)

    color_param = group.createColorParam("fillColor", "Fill Color", True)
    _set_display_range(color_param, 0, 1, 4)
    _set_default(color_param, (0.1, 0.65, 1.0, 1.0))
    _finalize(page, group, color_param, "fillColor", anim=True)

    tracking_param = group.createDoubleParam("tracking", "Tracking")
    _set_scalar_range(tracking_param, -500, 1000, -50, 200)
    _set_default(tracking_param, 0)
    _finalize(page, group, tracking_param, "tracking", anim=True)

    leading_param = group.createDoubleParam("leading", "Leading")
    _set_scalar_range(leading_param, 0, 5000, 0, 500)
    _set_default(leading_param, 0)
    _finalize(page, group, leading_param, "leading", anim=True)

    alignment_param = group.createChoiceParam("alignment", "Alignment")
    for option in ["Left", "Center", "Right"]:
        alignment_param.addOption(option, "")
    alignment_param.setDefaultValue(1)
    alignment_param.restoreDefaultValue()
    _finalize(page, group, alignment_param, "alignment", anim=True)

    frame_range_param = group.createInt2DParam("frameRange", "Frame Range")
    frame_range_param.setDefaultValue(1, 0)
    frame_range_param.setDefaultValue(100, 1)
    frame_range_param.restoreDefaultValue(0)
    frame_range_param.restoreDefaultValue(1)
    _finalize(page, group, frame_range_param, "frameRange")

    time_offset_param = group.createIntParam("timeOffset", "Time Offset (Frames)")
    time_offset_param.setDefaultValue(0, 0)
    time_offset_param.restoreDefaultValue(0)
    _finalize(page, group, time_offset_param, "timeOffset")

    before_param = group.createChoiceParam("before", "Before Behavior")
    for option in ["original", "hold", "black", "loop", "bounce"]:
        before_param.addOption(option, "")
    before_param.setDefaultValue(2)
    before_param.restoreDefaultValue()
    _finalize(page, group, before_param, "before", anim=True)

    after_param = group.createChoiceParam("after", "After Behavior")
    for option in ["original", "hold", "black", "loop", "bounce"]:
        after_param.addOption(option, "")
    after_param.setDefaultValue(2)
    after_param.restoreDefaultValue()
    _finalize(page, group, after_param, "after", anim=True)

    opacity_param = group.createColorParam("opacity", "Opacity", True)
    _set_display_range(opacity_param, 0, 1, 4)
    _set_default(opacity_param, (1, 1, 1, 1))
    _finalize(page, group, opacity_param, "opacity", anim=True)

    blend_param = group.createChoiceParam("blendingMode", "Blending Mode")
    for option in [
        "atop", "average", "color", "color-burn", "color-dodge", "conjoint-over",
        "copy", "difference", "disjoint-over", "divide", "exclusion", "freeze",
        "from", "geometric", "grain-extract", "grain-merge", "hard-light", "hue",
        "hypot", "in", "luminosity", "mask", "matte", "max", "min", "minus",
        "multiply", "out", "over", "overlay", "pinlight", "plus", "reflect",
        "saturation", "screen", "soft-light", "stencil", "under", "xor"
    ]:
        blend_param.addOption(option, "")
    blend_param.setDefaultValue(28)
    blend_param.restoreDefaultValue()
    _finalize(page, group, blend_param, "blendingMode", anim=True)


def _add_transform_params(group, page):
    translate_param = group.createDouble2DParam("translate", "Translate")
    _set_display_range(translate_param, -10000, 10000, 2)
    _finalize(page, group, translate_param, "translate", anim=True)

    rotate_param = group.createDoubleParam("rotate", "Rotate")
    _set_scalar_range(rotate_param, -3600, 3600, -180, 180)
    _finalize(page, group, rotate_param, "rotate", anim=True)

    scale_param = group.createDouble2DParam("scale", "Scale")
    for dim in range(2):
        scale_param.setMinimum(-10000, dim)
        scale_param.setMaximum(10000, dim)
        scale_param.setDisplayMinimum(0.1, dim)
        scale_param.setDisplayMaximum(10, dim)
        scale_param.setDefaultValue(1, dim)
        scale_param.restoreDefaultValue(dim)
    _finalize(page, group, scale_param, "scale", anim=True)

    uniform_param = group.createBooleanParam("uniform", "Uniform Scale")
    _set_default(uniform_param, False)
    _finalize(page, group, uniform_param, "uniform")

    skew_x_param = group.createDoubleParam("skewX", "Skew X")
    _set_scalar_range(skew_x_param, -3600, 3600, -100, 100)
    _finalize(page, group, skew_x_param, "skewX", anim=True)

    skew_y_param = group.createDoubleParam("skewY", "Skew Y")
    _set_scalar_range(skew_y_param, -3600, 3600, -100, 100)
    _finalize(page, group, skew_y_param, "skewY", anim=True)

    skew_order_param = group.createChoiceParam("skewOrder", "Skew Order")
    _set_default(skew_order_param, 0)
    _finalize(page, group, skew_order_param, "skewOrder")

    center_param = group.createDouble2DParam("center", "Center")
    _set_display_range(center_param, -10000, 10000, 2)
    _finalize(page, group, center_param, "center", anim=True)

    motion_blur_param = group.createDoubleParam("motionBlur", "Motion Blur")
    _set_scalar_range(motion_blur_param, 0, 100, 0, 4)
    _finalize(page, group, motion_blur_param, "motionBlur", anim=True)

    shutter_param = group.createDoubleParam("shutter", "Shutter")
    _set_scalar_range(shutter_param, 0, 2, 0, 2)
    _set_default(shutter_param, 0.5)
    _finalize(page, group, shutter_param, "shutter", anim=True)

    interactive_param = group.createBooleanParam("interactive", "Interactive Update")
    _set_default(interactive_param, True)
    _finalize(page, group, interactive_param, "interactive")


def _add_animator_storage_params(group, page):
    schema_param = group.createIntParam("motionTextSchemaVersion", "Motion Text Schema Version")
    schema_param.setDefaultValue(1, 0)
    schema_param.restoreDefaultValue(0)
    _finalize(page, group, schema_param, "motionTextSchemaVersion", hidden=True)

    next_id_param = group.createIntParam("animatorNextId", "Next Animator ID")
    next_id_param.setDefaultValue(1, 0)
    next_id_param.restoreDefaultValue(0)
    _finalize(page, group, next_id_param, "animatorNextId", hidden=True)

    order_param = group.createStringParam("animatorOrder", "Animator Order")
    order_param.setType(NatronEngine.StringParam.TypeEnum.eStringTypeDefault)
    _set_default(order_param, "[]")
    _finalize(page, group, order_param, "animatorOrder", hidden=True)

    stack_param = group.createStringParam("animatorStackJson", "Animator Stack JSON")
    stack_param.setType(NatronEngine.StringParam.TypeEnum.eStringTypeDefault)
    _set_default(stack_param, "{\"version\":1,\"animators\":[]}")
    _finalize(page, group, stack_param, "animatorStackJson", hidden=True)

    time_dependency_param = group.createDoubleParam("animatorTimeDependency", "Animator Time Dependency")
    _set_default(time_dependency_param, 0.0)
    _finalize(page, group, time_dependency_param, "animatorTimeDependency", anim=True, hidden=True)


def _create_node(app, plugin_id, version, group, script_name, label, position):
    node = app.createNode(plugin_id, version, group)
    if node is None:
        raise RuntimeError("Failed to create %s" % script_name)
    node.setScriptName(script_name)
    node.setLabel(label)
    node.setPosition(position[0], position[1])
    return node


def createInstance(app, group):
    group.setColor(0.1, 0.45, 0.7)

    motion_page = group.createPageParam("MotionTextSettings", "Motion Text")
    _add_motion_text_params(group, motion_page)
    _add_transform_params(group, motion_page)

    _add_animator_storage_params(group, motion_page)

    group.setPagesOrder(["MotionTextSettings", "Node", "Settings"])
    group.refreshUserParamsGUI()

    text_node = _create_node(app, "net.flux.openfx.TextRender", -1, group, "TextRender1", "TextRender1", (0, -200))
    frame_range_node = _create_node(app, "net.sf.openfx.FrameRange", 1, group, "FrameRange1", "FrameRange1", (0, -100))
    time_offset_node = _create_node(app, "net.sf.openfx.timeOffset", 1, group, "TimeOffset1", "TimeOffset1", (0, 0))
    transform_node = _create_node(app, "net.sf.openfx.TransformPlugin", 1, group, "Transform1", "Transform1", (0, 100))
    grade_node = _create_node(app, "net.sf.openfx.GradePlugin", 2, group, "Grade1", "Grade1", (0, 200))
    output_node = _create_node(app, "fr.inria.built-in.Output", 1, group, "Output1", "Output1", (0, 300))

    frame_range_node.connectInput(0, text_node)
    time_offset_node.connectInput(0, frame_range_node)
    transform_node.connectInput(0, time_offset_node)
    grade_node.connectInput(0, transform_node)
    output_node.connectInput(0, grade_node)

    group.getParam("text").setAsAlias(text_node.getParam("text"))
    group.getParam("font").setAsAlias(text_node.getParam("font"))
    group.getParam("fontStyle").setAsAlias(text_node.getParam("fontStyle"))
    group.getParam("fontSize").setAsAlias(text_node.getParam("fontSize"))
    group.getParam("fillColor").setAsAlias(text_node.getParam("fillColor"))
    group.getParam("tracking").setAsAlias(text_node.getParam("tracking"))
    group.getParam("leading").setAsAlias(text_node.getParam("leading"))
    group.getParam("alignment").setAsAlias(text_node.getParam("alignment"))
    group.getParam("animatorStackJson").setAsAlias(text_node.getParam("animatorStackJson"))
    group.getParam("animatorTimeDependency").setAsAlias(text_node.getParam("animatorTimeDependency"))
    group.getParam("frameRange").setAsAlias(frame_range_node.getParam("frameRange"))
    group.getParam("timeOffset").setAsAlias(time_offset_node.getParam("timeOffset"))
    group.getParam("before").setAsAlias(frame_range_node.getParam("before"))
    group.getParam("after").setAsAlias(frame_range_node.getParam("after"))
    group.getParam("opacity").setAsAlias(grade_node.getParam("multiply"))
    group.getParam("translate").setAsAlias(transform_node.getParam("translate"))
    group.getParam("rotate").setAsAlias(transform_node.getParam("rotate"))
    group.getParam("scale").setAsAlias(transform_node.getParam("scale"))
    group.getParam("uniform").setAsAlias(transform_node.getParam("uniform"))
    group.getParam("skewX").setAsAlias(transform_node.getParam("skewX"))
    group.getParam("skewY").setAsAlias(transform_node.getParam("skewY"))
    group.getParam("skewOrder").setAsAlias(transform_node.getParam("skewOrder"))
    group.getParam("center").setAsAlias(transform_node.getParam("center"))
    group.getParam("motionBlur").setAsAlias(transform_node.getParam("motionBlur"))
    group.getParam("shutter").setAsAlias(transform_node.getParam("shutter"))
    group.getParam("interactive").setAsAlias(transform_node.getParam("interactive"))
