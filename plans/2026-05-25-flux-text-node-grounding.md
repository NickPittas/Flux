# Flux Text Node Grounding Notes

Date: 2026-05-25

Purpose: capture the current After Effects text-animation findings and Flux-specific UX requirements before planning a native Flux text node/plugin. This is not an implementation plan yet.

## Direction

Text is a core motion-graphics feature, not a secondary layer type. Flux should treat text editing and text animation as first-class workflows.

The current `FluxText.py` PyPlug wraps Natron's native Text node and is acceptable only as Text v1. It is not the right long-term architecture for AE-style per-character, per-word, or per-line text animation.

Flux should move toward a native Flux text renderer/node, likely `FluxText.ofx` or equivalent native plugin, that lays out and renders text internally. It must not create one Text node per character, word, or line.

## After Effects Text Animator Model

AE text animation is built around animator groups:

```text
Text Layer
  Animator 1
    Properties: Position / Scale / Rotation / Opacity / Color / Tracking / etc.
    Selector(s): Range / Wiggly / Expression
  Animator 2
    ...
Text More Options
  Anchor/grouping alignment, per-character 3D, line anchor, etc.
```

The key model: animator properties are target deltas, not final values.

Example:

- Animator Position = `Y -100`
- Selector weight = `0.0` means the element stays at its base position.
- Selector weight = `0.5` means the element receives half the offset.
- Selector weight = `1.0` means the element receives the full offset.

Conceptually:

```text
final_element_value = base_value + animator_delta * selector_weight
```

Multiple animators can stack by applying or accumulating their weighted effects in order.

## Range Selector Semantics

Core controls:

- Start
- End
- Offset
- Amount
- Units
  - Percentage
  - Index
- Based On
  - Characters
  - Characters excluding spaces
  - Words
  - Lines

Expected Flux behavior should match the AE mental model:

- `Start = 0`, `End = 100`: first-to-last traversal.
- `Start = 100`, `End = 0`: last-to-first / reversed traversal.
- `Offset = -100..100`: shifts the selection wave across the element list.
- Users set a target property once, then animate selector controls to reveal or propagate the target through characters/words/lines.

Typical workflow:

1. Add text layer.
2. Add text animator.
3. Set animator target, e.g. Position `0, -100` or Opacity `0`.
4. Choose selector basis: characters, words, or lines.
5. Animate Offset, Start, or End.

## Selector Shapes / Falloff

Range selector shape controls the influence curve across selected elements:

- Square: hard on/off.
- Ramp Up: influence rises from 0 to 100.
- Ramp Down: influence falls from 100 to 0.
- Triangle: peak influence in the middle.
- Round: rounded peak/falloff.
- Smooth: softer eased falloff.

Additional controls:

- Ease High
- Ease Low

These ease the selector influence itself, separate from keyframe interpolation.

## Selector Extensions

AE also supports selector combinations and procedural selectors:

- Randomize Order
- Random Seed
- Selector Mode for multiple selectors:
  - Add
  - Subtract
  - Intersect
  - Difference
  - Min/Max-style combinations
- Wiggly Selector: random/noise variation per element.
- Expression Selector: procedural per-element weighting using concepts like `textIndex` and `textTotal`.

For Flux, Range Selector is the essential v1. Wiggly/Expression selectors can be planned as later extensions if the underlying element-weight model is designed cleanly now.

## Text Animator Properties

Important AE-style properties to support, with v1 candidates first:

### High-priority v1

- Position
- Scale
- Rotation
- Opacity
- Tracking
- Fill color
- Stroke/outline color
- Stroke/outline width

### Important follow-ups

- Kerning or kerning-like spacing control
- Anchor/grouping alignment
- Line anchor
- Skew / skew axis
- Blur
- Character offset / character value
- Per-character 3D-like controls if Flux later needs them:
  - Z position
  - X/Y/Z rotation
  - 3D scale/orientation

Transform properties must operate around the selected element's own center when appropriate:

- Character center for character-based animation.
- Word center for word-based animation.
- Line center for line-based animation.

## Native Text Node Implementation Implication

A Flux-native text node/plugin must internally:

1. Shape and lay out the full text.
2. Split the laid-out result into addressable elements:
   - grapheme/glyph clusters for character animation,
   - words,
   - lines.
3. Store each element's base transform, bounds, baseline, advance, and center.
4. Evaluate selector weights per element at render time.
5. Apply animator deltas around the relevant element/group center.
6. Render all elements in one plugin render pass.

This avoids node explosion and keeps performance viable for hundreds or thousands of text elements.

## Text Layout Requirements

The renderer should be designed around real text layout, not a simple ASCII character loop.

Important concerns:

- Font family and variant/style selection.
- Font size.
- Font weight/style/stretch where available.
- Paragraph alignment/justification.
- Text box vs point text.
- Line wrapping.
- Line spacing / leading.
- Tracking.
- Kerning.
- Baseline and ascender/descender metrics.
- Glyph clusters rather than naive bytes/chars, to avoid breaking ligatures, accents, emoji, or complex scripts.
- Bidirectional text and shaping should not be painted into a corner, even if v1 support is Latin-first.

## Flux UI / Panel Requirement

Text controls should not be buried only inside a generic layer property panel. Motion graphics users need fast text operations from dedicated panels that act on the selected text layer in the timeline.

Likely panel direction:

### Character / Text Panel

Applies to the currently selected Flux text layer:

- Font family.
- Font variant/style/weight.
- Font size.
- Fill color.
- Stroke/outline color.
- Stroke/outline width.
- Tracking.
- Kerning.
- Baseline shift if supported.
- Faux bold/italic only if technically needed and clearly marked.

### Paragraph Panel

Applies to selected text layer or selected paragraph range when richer editing exists:

- Left / center / right align.
- Justify modes.
- Vertical alignment inside text box.
- Line spacing / leading.
- Paragraph spacing before/after if needed.
- Text box margins/insets if box text is supported.

### Text Animator Panel

Purpose-built for motion graphics animation, not raw knob hunting:

- Add Animator.
- Add/remove animator properties.
- Choose Based On: characters, characters excluding spaces, words, lines.
- Range Selector controls: Start, End, Offset, Amount.
- Selector shape/falloff controls.
- Randomize Order / Seed later.
- Show animator rows under the text layer in the timeline/keyframe tree.

These panels should drive the selected text layer's native knobs/parameters, so animation still uses Natron/Flux's existing Knob/Curve system where practical.

## Timeline Integration Notes

Text layer selection in the timeline should be the center of the workflow:

- Selecting a text layer activates the text-specific panels.
- Common text formatting actions should target the selected text layer immediately.
- Text animator groups should appear as expandable children under the text layer, similar to AE's timeline hierarchy.
- Animated text properties should participate in the existing Flux keyframe row model.
- Selector Offset/Start/End should be keyframeable and visible in the timeline.

## Current Flux State / Gap

Current Text v1:

- `plugins/FluxText.py` wraps one native Text node.
- Text controls are promoted as `Text1...` knobs.
- The text is rendered as one object.
- This is useful for basic text layers, but not sufficient for AE-style text animators.

Known existing issue:

- Native Text justification/alignment is broken independently of FluxText v1 and is tracked separately as T072.

The long-term text system should not depend on fixing or extending the old native Text node if a Flux-native renderer is more controllable.

## Planning Principle

The first planning pass should define the data model and plugin boundary before UI polish:

1. Native text layout/rendering capability.
2. Internal element model for characters/words/lines.
3. Animator + selector parameter schema.
4. Knob/Curve integration for keyframes.
5. Dedicated Character/Paragraph/Text Animator panels connected to the selected timeline text layer.

The goal is a text system that feels like a motion-graphics tool first, while still fitting Flux's Natron/OpenFX architecture.
