# T074 Automated Debug Ledger

## Standing Instructions from Nick

- No manual validation from Nick from this point forward.
- Do not ask Nick to create layers, add keyframes, switch panels, or inspect UI state.
- The agent must drive the application itself, capture screenshots, and provide evidence.
- Do not assume behavior from logs alone.
- Any assumption must be reviewed through scrutinize-style review and oracle before being treated as actionable.
- Every step taken for this debug thread must be documented in this file.
- Every turn must read this file after the mandatory workspace docs.
- After context compaction, this file must be re-read before continuing.
- Do not open Flux/Natron merely to wait for manual interaction.

## Current Failure Definition

Native DopeSheet shows nothing useful for Flux-created animated layer/text keyframes. The required end state is an agent-driven repro that creates actual keyframes, switches/opens the native DopeSheet, captures a screenshot, and proves whether keyframes are visible.

## Method Correction

Previous runs were invalid as autonomous validation because Nick manually created the text layer and keyframes. Logs from those runs prove only that Nick interacted with the app; they do not prove agent automation or successful validation.

## Ledger

### 2026-05-25 — Instruction reset

- Read mandatory docs: `AGENTS.md`, `ARCHITECTURE.md`, `plans/PHASES.md`, `tasks/TASKS.md`, and collaboration manifesto.
- Created this ledger file to make the new operating contract durable across turns and compaction.
- No application launch, test, or fix was performed in this step.

### 2026-05-25 — Proof standard clarified

- Read mandatory docs and this ledger at start of turn.
- Nick clarified that previous manual-attempt logs are a starting point only. They may be used as breadcrumbs, but absolute proof requires an autonomous harness that produces the needed result without Nick touching the UI.
- Loaded `scrutinize` workflow guidance for plan review discipline.
- Requested oracle review of the autonomous proof plan. Oracle verdict: concept approved only after tightening proof criteria and process safety; do not implement loose plan as-is.
- Oracle-required proof contract before implementation:
  - fresh autonomous launch command;
  - generated node/layer name;
  - knob name and native curve key times/count;
  - DopeSheet model contains expected node/knob row;
  - native DopeSheet panel is active/visible;
  - hierarchy expanded/scrolled and time range zoomed so target row/keyframes are in view;
  - screenshot path;
  - screenshot crop or image-analysis assertion proving visible key diamonds/pixels near expected coordinates.
- Oracle process constraints:
  - do not kill unknown Flux/Natron processes; only terminate agent-owned PIDs recorded in this ledger;
  - prefer a temporary/env-gated Qt-side validation harness if Python cannot reliably switch/grab the DopeSheet;
  - use production creation/keyframe APIs, not fake DopeSheet rows;
  - no production behavior fix until autonomous visual repro exists.
- No app launch, test, or fix was performed in this step.

### 2026-05-25 — Autonomous implementation started

- Read mandatory docs and this ledger at start of turn.
- Nick instructed work to proceed without further manual validation; auto-continue may run unattended.
- Started an explicit todo list for autonomous proof harness work.
- Next step is read-only discovery of existing automation hooks and relevant Flux/DopeSheet APIs before adding or changing code.
- No app launch, test, or fix was performed in this step.

### 2026-05-25 — Harness API discovery and detailed review

- Read-only discovery completed via explorer and direct source reads.
- Relevant discovered APIs:
  - `Gui::getFluxTimeline()`, `Gui::getDopeSheetEditor()`, and `Gui::rebuildCompositingGraph()` in `Gui/Gui.h`.
  - `FluxTimeline::addTextLayer()` and `FluxTimeline::getLayers()` in `Gui/FluxTimeline.h`.
  - Flux layout places Timeline, DopeSheet, and Curve Editor in `fluxWorkshopPane` in `Gui/Gui05.cpp`.
  - `DopeSheetEditor::getDopesheetView()`, `getHierarchyView()`, `centerOn()`, and `refreshSelectionBboxAndRedrawView()` in `Gui/DopeSheetEditor.h`.
  - `DopeSheet` model has `getItemNodeMap()`, `findDSNode()`, and `findDSKnob()` in `Gui/DopeSheet.h`.
  - `DopeSheetView::toWidgetCoordinates()` is public and can map key time to widget X after `centerOn()`.
  - `TabWidget::setCurrentWidget()` can activate the DopeSheet tab.
  - `Gui::screenShot(QWidget*)` and QWidget `grab()` can capture proof images.
- Oracle reviewed the detailed harness design. Verdict: approved with changes.
- Required design changes from oracle:
  - add a tiny DopeSheet proof API instead of inferring model evidence from tree text alone;
  - use `DopeSheetView::toWidgetCoordinates()` for exact key X positions, not approximate `time/80 * width`;
  - call `setAnimationEnabled(true)` before writing keys;
  - image proof should sample/diff boxes around exact key coordinates.
- No app launch, test, or fix was performed in this step.

### 2026-05-25 — Env-gated proof harness implemented

- Read mandatory docs and this ledger at start of turn.
- Loaded the debug-mantra discipline because T074 is an active debug task.
- Added env-gated harness files `Gui/FluxT074Harness.h` and `Gui/FluxT074Harness.cpp`.
- Added `DopeSheetEditor::getModelForT074Proof()` as a narrow proof API exposing the native DopeSheet model to the harness.
- Hooked `FluxT074Harness::maybeStart(this)` at the end of `Gui::setupFluxUi()`; it is inert unless `FLUX_T074_AUTOMATED_PROOF=1` is set.
- Updated `Gui/CMakeLists.txt` to explicitly include the new harness files so the current configured build links them.
- Build command run: `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`.
- Build result: passed after adding the harness files to `Gui/CMakeLists.txt`. Initial link failure before that was `undefined reference to Natron::FluxT074Harness::maybeStart(Natron::Gui*)`, proving the new source was not in the configured target yet.
- First autonomous proof run command: `QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb FLUX_T074_AUTOMATED_PROOF=1 /home/npittas/Flux/build/App/Natron`.
- First autonomous report: `/tmp/opencode/t074-proof-1428505/report.json`.
- First autonomous result: failed before screenshots because the native curve had 2 keys at frames `[1, 75]`, but the native DopeSheet model had no DSNode/DSKnob for `FluxText1` / `Text1rotate`.
- Oracle reviewed the autonomous report and accepted it as satisfying the reproduce-before-fix requirement for this failure mode.
- Oracle-reviewed likely root cause: Flux-managed nodes are created with `SettingsOpened=false`; unless their `NodeGui` settings panel is created, `NodeGui::ensurePanelCreated()` never registers them with native CurveEditor/DopeSheet.
- Oracle-approved minimal fix to test: use the native registration path by creating the Flux gizmo node panel hidden/minimized via `NodeGui::ensurePanelCreated(true, true)` after Flux gizmo creation; do not fabricate DopeSheet rows and do not duplicate animation state.
- Implemented the minimal production fix in `Gui/Gui05.cpp` via helper `ensureFluxNodeRegisteredWithAnimationEditors()` and call after assigning a new layer gizmo.
- Adjusted env-gated harness shutdown to `_Exit(0)` after flushing/writing the report, avoiding the observed Natron teardown SIGSEGV after report generation. This is env-gated validation behavior only, not production runtime behavior.

### 2026-05-25 — Corrected hidden-knob regression from registration helper

- Read mandatory docs and this ledger at start of turn.
- Nick reported that the Text layer properties lost all knobs except center after the DopeSheet registration fix.
- Root cause identified: the new registration helper called `NodeGui::ensurePanelCreated(true, true)`. The second argument is `hideUnmodified`; using `true` created the hidden/minimized panel for DopeSheet registration while also filtering out unmodified knobs in the visible properties UI.
- Fix applied in `Gui/Gui05.cpp`: changed the registration call to `nodeGui->ensurePanelCreated(true, false)` so the panel is hidden/minimized for registration but does not hide unmodified Text controls.
- Build command run: `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`.
- Build result: passed.
- Autonomous proof rerun command: `QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb FLUX_T074_AUTOMATED_PROOF=1 /home/npittas/Flux/build/App/Natron`.
- Autonomous proof report: `/tmp/opencode/t074-proof-1637844/report.json`.
- Result: passed; native curve key count 2 at frames `[1, 75]`, DopeSheet node/knob found, DopeSheet active, row visible, image assertion passed with sample counts 128 and 120.
- Remaining cleanup before final: remove `[T074-PROBE]` production logging and rerun autonomous proof.

### 2026-05-25 — Production debug logging cleanup and final autonomous proof

- Read mandatory docs and this ledger at start of turn.
- Removed remaining `[T074-PROBE]` instrumentation and temporary probe helpers from production paths:
  - `Gui/DopeSheet.cpp`
  - `Gui/DopeSheetHierarchyView.cpp`
  - `Gui/DopeSheetView.cpp`
  - `Gui/Gui05.cpp`
  - `Gui/FluxKeyframeModelOps.cpp`
  - `Gui/KnobGui10.cpp`
  - `Engine/KnobImpl.h`
  - `Engine/Knob.cpp`
- Verified cleanup with source search: no remaining `T074-PROBE`, `t074ProbeNode`, `t074KeyCount`, or `t074HasProbeNode` hits in C++/header production files.
- Preserved the functional fix path:
  - Flux gizmos are registered with native animation editors through `NodeGui::ensurePanelCreated(true, false)`.
  - Flux-managed DopeSheet nodes are not hidden merely because their settings panel is hidden.
  - Flux group/gizmo nodes draw keyframes in the native DopeSheet.
  - DopeSheet visibility/drawing uses authoritative engine curves for PyPlug alias knobs where needed.
- Build command run: `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`.
- Build result: passed.
- Autonomous proof rerun command: `QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb FLUX_T074_AUTOMATED_PROOF=1 /home/npittas/Flux/build/App/Natron`.
- Autonomous proof report: `/tmp/opencode/t074-proof-1813282/report.json`.
- Result: passed; native curve key count 2 at frames `[1, 75]`, DopeSheet node/knob found, DopeSheet active, row `Rotate` visible, image assertion passed with sample counts 128 and 120.
- Screenshots captured:
  - `/tmp/opencode/t074-proof-1813282/main-window.png`
  - `/tmp/opencode/t074-proof-1813282/dopesheet-editor.png`
  - `/tmp/opencode/t074-proof-1813282/dopesheet-view.png`

### 2026-05-25 — Nick validation and task closeout

- Read mandatory docs and this ledger at start of turn.
- Nick reported that after testing, everything seems to work, and explicitly requested committing the work.
- Updated T074 status to `DONE` in `tasks/TASKS.md` and `tasks/T074-flux-timeline-keyframe-editor.md`.
- Updated `plans/PHASES.md` P7 progress and T074 summary to reflect the completed/validated task.
