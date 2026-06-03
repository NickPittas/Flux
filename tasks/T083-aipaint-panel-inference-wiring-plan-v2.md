# Planner Report

## Status
ready

## Rationale
This revision is scoped to one coherent implementation unit: connect the selected timeline source and selected layer AI Paint effect to the existing AI Panel/SAM3 still-mask path. The revised packet removes the prior ambiguity by naming the existing source-frame export API/call sequence and by defining the exact selected AI Paint provider connector in `Gui05.cpp`, with narrow stop conditions if either connector cannot be implemented inside the authorized files.

# Task Packet

## User Goal
Wire native AI Paint viewer-owned point/box prompts into the Flux AI Panel so the panel can summarize prompts and run inference from the selected AI Paint/source.

## Mode
general-coding

## Relevant Locations
- file: `Gui/Gui.h`
  symbol: `Gui::exportFluxSam3SourceFrameForSelectedLayer(QJsonObject* sourceMetadata = 0)`
  approximate lines: 88-94
  stable anchor: `Export current selected Flux footage source frame as a temporary PNG for external SAM3 inference.`
  reason: Existing callable API for AI Panel source-frame still capture; `FluxAiPanel` already stores `_gui` and includes `Gui/Gui.h`.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::exportFluxSam3SourceFrameForSelectedLayer`
  approximate lines: 523-690
  stable anchor: `FLUX-SAM3-A1 capture complete`
  reason: Exact still-frame export implementation: validates selected timeline footage layer, computes `sourceFrame = timelineFrame - layer.timeOffset`, writes temp PNG under `QDir::temp()/Flux/sam3_preview`, and fills dimensions/source metadata.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `FluxTimeline::sourceViewerRequested` lambda
  approximate lines: 1064-1125
  stable anchor: `aiPanel->setViewerForCapture`
  reason: Current source-viewer arming path connects viewer input 0 to `layer.readerNode` but does not yet pass full source or AI Paint provider context to the panel.
  confidence: high
- file: `Gui/FluxTimeline.h`
  symbol: `FluxLayer`, `FluxEffect`, `FluxTimeline::getSelectedLayerIndex`, `getLayers`, `getCurrentFrame`, `sourceViewerRequested(int)`
  approximate lines: 45-125, public API below class declaration
  stable anchor: `QList<FluxEffect> effects;`
  reason: Provides selected layer/effect list needed by the connector to find the selected layer's AI Paint node.
  confidence: high
- file: `Engine/AIPaint.h`
  symbol: `class AIPaint`
  approximate lines: 35-95
  stable anchor: `std::unique_ptr<AIPaintPrivate> _imp;`
  reason: Native AI Paint owns prompt context privately; add the smallest public read-only accessor for prompts.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `AIPaint::initializeKnobs`, `AIPaint::onKnobsLoaded`, `AIPaint::knobChanged`, overlay pen handlers, `AIPaint::getPluginID`
  approximate lines: 132, 202-310, 400-454
  stable anchor: `kAIPaintParamPromptStore`, `_imp->context.addPoint`, `_imp->context.addBox`, `return PLUGINID_NATRON_AIPAINT;`
  reason: Confirms AI Paint persists prompts in the hidden prompt store/context and identifies AI Paint by plugin id.
  confidence: high
- file: `Engine/AIPaintContext.h`
  symbol: `AIPaintContext`, `AIPaintPrompt`
  approximate lines: 31-68
  stable anchor: `std::vector<AIPaintPrompt> prompts() const;`
  reason: Defines prompt fields/types/roles and already exposes the read-only vector needed by `AIPaint` accessor.
  confidence: high
- file: `Engine/AIPaintContext.cpp`
  symbol: `AIPaintContext::serialize`, `AIPaintContext::deserialize`
  approximate lines: 143-240
  stable anchor: `root.insert(QString::fromUtf8("prompts"), promptArray);`
  reason: Defines persistent prompt JSON contract; do not change it except compatible additions.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel`
  approximate lines: 20-110
  stable anchor: `setSourceCaptureContext`, `startSam3StillMask`, `buildSourcePointPrompt`, `buildSourceBoxPrompt`
  reason: Add selected AI Paint provider state/API and prompt conversion helpers here.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `refreshSourceFrameMetadata`, `updatePromptSummary`, `onRunClicked`, `startSam3StillMask`, `verifySam3ResultAndWriteManifest`
  approximate lines: 350-735
  stable anchor: `refreshSourceFrameMetadata` returning `false`, RotoPaint prompt text, `selected_prompt`, `prompts`
  reason: Replace stub/RotoPaint wording with AI Paint summary, source-frame export handoff, and SAM3 run path.
  confidence: high
- file: `tasks/T083-ai-matte-depth.md`
  symbol: T083 approved prompt/output policy
  approximate lines: 5-35
  stable anchor: `Unsaved projects must force Save As before AI generation`
  reason: Task constraints: viewer toolbar owns prompts; AI Panel summarizes/runs; generated media is project-relative; unsaved projects force Save As.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `Gui/FluxAiWorkerController.h`
- `Gui/FluxAiWorkerController.cpp`
- `Gui/Gui.h`
- `Gui/FluxTimeline.h`
- `tasks/T083-ai-matte-depth.md`

## Required Change
1. Keep native AI Paint as the prompt source of truth.
   - Do not add point/box controls to `FluxAiPanel`.
   - Do not modify or hijack RotoPaint.
   - Viewer overlay prompts must remain viewer-only and must not affect RGBA render.
2. Add the minimal read-only prompt access surface in `AIPaint`.
   - Include `Engine/AIPaintContext.h` in `Engine/AIPaint.h` if needed.
   - Add a public method such as `std::vector<AIPaintPrompt> getPrompts() const;` implemented in `AIPaint.cpp` as a direct copy return from `_imp->context.prompts()`.
   - Do not change `AIPaintContext` serialization format or prompt persistence unless required for a backward-compatible field.
3. Implement explicit selected AI Paint/provider discovery in `Gui05.cpp` source-viewer wiring.
   - In the `FluxTimeline::sourceViewerRequested` lambda, after validating `layerIndex` and `layer`, scan only `layer.effects` for the selected layer.
   - For each `FluxEffect`, require `effect.enabled`, `effect.node`, `effect.node->isActivated()`, `effect.node->getEffectInstance()` non-null, and `effect.node->getEffectInstance()->getPluginID() == PLUGINID_NATRON_AIPAINT`.
   - Use the first matching AI Paint effect in layer order as the v1 provider; do not scan unrelated layers, masks, RotoPaint nodes, or the whole node graph.
   - Pass that provider node to the panel with source context. If no matching AI Paint effect exists, pass an empty provider and keep Run disabled with a clear “selected layer has no AI Paint effect” summary.
   - If `PLUGINID_NATRON_AIPAINT` is not visible from `Gui05.cpp` through existing includes, include the narrow existing header that defines it; do not hardcode the plugin id string unless the constant is unavailable in the authorized files.
4. Extend `FluxAiPanel` source context API/state narrowly.
   - Add a member such as `NodePtr _sourceAIPaintNode;` and update `setSourceCaptureContext(...)` to accept the selected AI Paint provider node.
   - Reset `_sourceAIPaintNode` in `setViewerForCapture()` and when source context is cleared/stale.
   - Add private helpers in `FluxAiPanel.cpp` to resolve `_sourceAIPaintNode->getEffectInstance()`, verify plugin id `PLUGINID_NATRON_AIPAINT`, cast to `AIPaint`, and read current prompts through the new accessor.
   - Stop instead of guessing if the selected provider cannot be verified as AI Paint through `Node::getEffectInstance()` / `EffectInstance::getPluginID()`.
5. Replace the source-frame metadata stub with the exact existing export call sequence.
   - In `FluxAiPanel::refreshSourceFrameMetadata(QString* message)`, call `_gui->exportFluxSam3SourceFrameForSelectedLayer(&metadata)`.
   - Treat the returned path as the source PNG for inference only; it may remain under the existing temp preview path because `exportFluxSam3SourceFrameForSelectedLayer()` marks it temporary and the generated outputs/manifests remain project-relative.
   - Require the returned file to exist and have nonzero size.
   - Require metadata dimensions from `width`/`height` or `exported_png_width`/`exported_png_height` to be > 0 and internally consistent.
   - Store the returned path in panel state for the next immediate run (for example `_sourceFramePng`), set `_sourceFrameMetadata`, `_sourceFrameWidth`, `_sourceFrameHeight`, and `_sourceFrameMetadataFresh = true`.
   - Do not invent another capture/export path. If this API cannot be called safely from the panel, stop and report that the existing export connector failed.
6. Preserve project-relative generated output and Save As preflight.
   - Before calling `refreshSourceFrameMetadata()` or creating any output directory in `onRunClicked()`, verify the project has a saved project path using the existing project/output policy already used in this file/nearby worker controller.
   - If the project is unsaved, force the existing Save As flow if there is an existing GUI API for it in authorized files; otherwise block Run with a clear Save As-required message and write no source PNG, output directory, result manifest, or generated media.
   - It is acceptable for the input source PNG to be temporary; output root, mask path, probe path, and result manifest must remain project-relative as already recorded by `verifySam3ResultAndWriteManifest()`.
7. Update AI Panel UI behavior and wording.
   - Replace all RotoPaint prompt text in `setupUi()`, `refreshSourceFrameMetadata()`, `updatePromptSummary()`, `onApplyClicked()`, and `onRunClicked()` with AI Paint wording.
   - `updatePromptSummary()` should read current AI Paint prompts and summarize enabled point/box prompts: total enabled points, total enabled boxes, selected enabled prompt if any, newest fallback if none selected, and clear disabled reasons (`no selected source`, `no AI Paint effect on selected layer`, `no point/box prompts`, `source/frame mismatch` when detectable).
   - Do not draw prompts in the panel.
8. Implement deterministic v1 prompt selection and conversion in `onRunClicked()`.
   - Re-read AI Paint prompts at click time from the selected AI Paint provider.
   - Consider only enabled `AIPaintPromptType::Point` and `AIPaintPromptType::Box` prompts.
   - Prefer an enabled prompt with `selected == true`; if none, choose the newest enabled point/box by highest `id`.
   - Require the prompt `time` to match `_sourceSourceFrame` or the current prompt/source time policy if `time` is meaningful; if existing AI Paint stores viewer timeline time rather than source time and no exact policy is evident, do not reject solely on time, but include both prompt time and source/timeline frame in the selected prompt provenance.
   - Convert point prompts to the existing viewer-prompt JSON expected by `buildSourcePointPrompt()` using canonical coordinates from `AIPaintPrompt::point` under `canonical.x/y`, then call `buildSourcePointPrompt()`.
   - Convert box prompts to the existing viewer-prompt JSON expected by `buildSourceBoxPrompt()` using `AIPaintPrompt::rect` under `canonical_min` and `canonical_max`, then call `buildSourceBoxPrompt()`.
   - Set `_sam3Prompt` to the converted source prompt and include provenance fields: AI Paint prompt `id`, `type`, `role`, `enabled`, `selected`, `time`, `coordinateSpace`, `label`, `backendTag`, and metadata when available.
   - For point role semantics, map `AIPaintPromptRole::Include` to SAM label `1`. If `Exclude` prompts are encountered, only map to `0` if the existing SAM3 command path and provider support negative labels; otherwise block with a clear unsupported-exclude message. Neutral prompts are unsupported in v1 and should block if selected/chosen.
9. Wire Run to existing SAM3 still-mask path.
   - `updateUiState()` should enable Run only when not running, model id is `sam3_transformers`/available enough for the existing path, a selected source is valid, an AI Paint provider exists, and at least one enabled point/box prompt exists.
   - In `onRunClicked()`, after Save As/project preflight, call `refreshSourceFrameMetadata(&message)`, validate with `validateSourceCaptureContext(&message)`, convert the selected prompt, compute project-relative run root exactly like the existing generated-media policy in this file/worker controller, then call `startSam3StillMask(modelId, runId, absoluteRoot, relativeRoot, sourcePng, _sourceFrameMetadata)`.
   - Do not redesign the SAM backend, add multi-prompt batching, or change provider runtime scripts.
10. Preserve result manifest contract.
   - Ensure `verifySam3ResultAndWriteManifest()` continues writing project-relative output paths.
   - Keep `selected_prompt` and `prompts` populated with the chosen AI Paint prompt JSON/provenance. If `prompts` is currently a single object, keep it backward-compatible; do not require a multi-prompt schema for this v1.

## Non-Goals
- Do not modify or hijack native RotoPaint.
- Do not move point/box prompt controls into the AI Panel.
- Do not convert viewer overlays into renderable RGBA output.
- Do not redesign SAM/provider runtime, add multi-prompt SAM batching, create nodegraph AI suites, or convert prompts to Roto splines.
- Do not scan the whole node graph for AI Paint providers.
- Do not broaden into model-manager/download work beyond honoring existing missing-model/provider status.
- Do not edit task status files in this implementation packet.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- If the build directory/target name differs, run the project’s existing equivalent GUI target build and report the exact command used.

Expected result:
- Build succeeds.
- Manual GUI validation: in a saved project, add/select a footage layer with an AI Paint effect, arm the source viewer from the timeline source-viewer flow, add AI Paint point and box prompts in the viewer toolbar, and confirm the AI Panel summarizes AI Paint prompts without RotoPaint wording.
- Manual connector validation: selected layer with no AI Paint effect keeps Run disabled and reports no AI Paint effect; selected layer with AI Paint effect and prompts enables Run only when source/model/preflight conditions are satisfied.
- Manual generation validation: with SAM3 installed/available, Run exports the source still through `Gui::exportFluxSam3SourceFrameForSelectedLayer(&metadata)`, launches existing SAM3 still-mask inference, writes mask/probe/manifest under a project-relative generated-media run root, and manifest includes source metadata plus selected AI Paint prompt provenance.
- Missing-provider validation: with SAM3 missing/unavailable, panel blocks with a clear missing-model/provider message and writes no generated media.
- Unsaved-project validation: start from an unsaved project and attempt Run; Save As must be forced or Run must block before any temp source PNG, output directory, source PNG copy, generated mask, probe result, or manifest is written.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- selected AI Paint/source discovery cannot be implemented by scanning `layer.effects` in `Gui05.cpp`
- the selected provider cannot be verified with `Node::getEffectInstance()` and `EffectInstance::getPluginID() == PLUGINID_NATRON_AIPAINT`
- `Gui::exportFluxSam3SourceFrameForSelectedLayer(&metadata)` cannot be safely called from `FluxAiPanel::refreshSourceFrameMetadata()`
- implementation would add AI Panel prompt capture controls or modify RotoPaint
- implementation would write generated media before Save As/project-relative output is established
- implementation requires changing provider runtime scripts or SAM backend behavior

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
