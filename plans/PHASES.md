# Flux — Phase Breakdown

## Phase Status Overview

| Phase | Name | Status | Start | End | Progress |
|---|---|---|---|---|---|
| P0 | Project Setup | DONE | 2026-05-20 | 2026-05-20 | 100% |
| P1 | Fork & Build | DONE | 2026-05-20 | 2026-05-20 | 100% |
| P2 | UI Shell | DONE | 2026-05-20 | 2026-05-21 | 100% |
| P3 | Timeline | DONE | 2026-05-21 | 2026-05-22 | 100% |
| P4 | Effects + Properties | DONE | 2026-05-22 | 2026-05-22 | 100% |
| P5 | Import/Export | DONE | 2026-05-22 | 2026-05-23 | 100% |
| P6 | Timeline Tree + Masks | DONE | 2026-05-23 | 2026-05-23 | 100% |
| P7 | Shapes + Text | IN_PROGRESS | 2026-05-24 | — | 45% |
| P8 | Polish + Cache | PENDING | — | — | 0% |

---

## P0: Project Setup

**Goal**: Establish project infrastructure, plans, task tracking, and workspace conventions.

**Deliverables**:
- [x] AGENTS.md with project-specific guidelines
- [x] plans/PHASES.md with phase breakdown
- [x] tasks/TASKS.md with task tracking
- [x] plans/phase-1.md with detailed P1 plan
- [x] ARCHITECTURE.md updated to reflect Qt+Natron fork approach
- [x] Git repo initialized with Natron fork

**Exit Criteria**: All infrastructure files created, ARCHITECTURE.md updated, ready to begin P1.

---

## P1: Fork & Build

**Goal**: Fork Natron, build it on Linux with Qt6, validate the full pipeline (import, OCIO, rendering), document the Engine API.

**Completed**: 2026-05-20

**Actual Duration**: 1 day

**Results**:
- Forked Natron RB-2.6 → rebased onto `gui-sbk6` branch for Qt6 support
- Built with Qt6 6.11.1, GCC 16.1, Python 3.14, Fedora 44
- Fixed: Python 3.14 PyConfig API, Shiboken6 compat, qhttpserver Qt6 compat, NodeGroup char16_t
- OpenFX-IO plugin built with OIIO 3.1.12, FFmpeg, OCIO 2.4.2, OpenEXR 3.2.4
- OpenFX-Misc plugin built (Merge, Transform, ColorCorrect, etc.)
- GUI launches on Wayland (via xcb + OpenGL 4.6 / RTX 4090)
- File import validated: jpg, png, mov, mp4 via ReadOIIO/ReadFFmpeg
- OCIO validated: working with Nuke OCIO config
- Engine API documented: node system, rendering/cache, params/serialization

---

## P2: UI Shell

**Goal**: Replace Natron's node-graph GUI with Flux's layer-based motion graphics UI.

**Started**: 2026-05-20 | **Completed**: 2026-05-21

**Actual Duration**: 2 days

**Results**:
- Dark theme applied (After Effects-inspired, Natron's mainstyle.qss with Flux color palette)
- FluxMainWindow layout: Project Bin (20%) | Viewport (50%) | Effects+Properties (30%) top, Timeline (30%) bottom
- FluxProjectBin: thumbnail grid view, drag-and-drop import, file import to reader nodes
- Flux Menu System: File/Edit/Layer/Composition/View/Window/Help menu bar for Flux mode, with NodeGraph edit actions, timeline layer actions, and pane focus actions; Natron menu path preserved
- FluxTimeline: custom-painted layer bars, playhead synced to Natron's shared TimeLine, drag/trim/reorder
- FluxEffectsPanel: effect stack per layer, add effect button, active only when layer selected
- FluxLayer PyPlug gizmo (`net.sf.openfx.FluxLayer`): Input→FrameRange→TimeOffset→Transform→Multiply→Output wrapped in one group node per footage layer, with stable parameter aliases (frameRange, timeOffset, translate, scale, rotate, center, motionBlur, shutter)
- Compositing graph: non-destructive Merge chain outside gizmos, external Read per footage layer, Flux Background/Reformat anchor, final output to viewer
- Drag-and-drop from Project Bin to Timeline: creates layer, gizmo, deferred file probe + parameter init
- Trim left/right: updates FrameRange knob via explicit trimStart/trimEnd state
- Move: updates TimeOffset knob only (never touches FrameRange)
- Deferred init with retry: 200ms probe, 300ms retry if file range not yet available, nodeInitialized guard prevents overwrite
- Gizmo preservation: rebuildCompositingGraph skips layers with existing gizmoNode
- Node graph layout: staggered gizmo + merge node positioning

**Key Files**:
- `Gui/Gui05.cpp` — setupFluxUi(), rebuildCompositingGraph(), deferredInitGizmoParams()
- `Gui/FluxTimeline.{h,cpp}` — FluxLayer struct, timeline widget, interactions
- `Gui/FluxProjectBin.{h,cpp}` — project bin with drag export
- `Gui/FluxEffectsPanel.{h,cpp}` — effects stack UI
- `plugins/FluxLayer.py` — PyPlug gizmo (installed to ~/.Natron/PyPlugs/)

---

## P3: Timeline (COMPLETE)

**Goal**: Full-featured timeline with playback controls, layer types, solo/mute/lock, keyboard shortcuts.

**Started**: 2026-05-21

**Completed**: 2026-05-22

**Dependencies**: P2 complete

**Completed so far**:
- Playback controls — viewer drives shared TimeLine, FluxTimeline follows via onExternalFrameChanged
- Keyboard shortcuts — JKL play fwd/back/stop, arrows for frame stepping
- Layer types: solid (FluxSolid PyPlug with Constant), null (no gizmo), footage via drag-drop
- Context menu: Add Solid, Add Null, Delete Layer, Duplicate Layer, Split Layer
- Transform overlay handles on gizmo via addTransformInteract
- Properties panel opens/closes on layer select/deselect
- Trim/move model rewritten: complete separation of frameRange (trim only) and timeOffset (move only)
- Desaturated bar zones for extended trim regions
- Bar clipped at left panel boundary
- Duplicate Layer: Ctrl+D keyboard shortcut + context menu; native Natron clipboard copy/paste, Read+Gizmo+Merge for footage, Gizmo+Merge for solids
- Split Layer: duplicate + trim original outPoint / duplicate inPoint at playhead
- Delete Layer: deactivates Read/Gizmo/Merge and rebuild reconnects the remaining chain
- Solo/Mute/Lock per layer: L button toggle in layer header; locked layers block trim, move, reorder, delete, split, and duplicate
- Background/Reformat anchor: persistent Reformat node at top of chain, inputs forcibly disconnected each rebuild
- Non-destructive rebuild: existing Read/Gizmo/Merge nodes reused; only missing nodes created, reconnect + reposition only
- External Read node: footage layers have Read outside gizmo; gizmo uses internal Input node
- Flux-managed creation: AutoConnect=false, AddUndoRedoCommand=false, SettingsOpened=false
- Merge input mapping: 0=B/background, 1=A/foreground (documented and enforced)
- Project Bin: empty-space double-click opens import; item double-click adds footage as layer
- Timeline duration synced to project frame range via Project::frameRangeChanged signal
- Context menu complete: Add Solid/Null, Delete, Duplicate, Split, Add Effect, Reset In/Out Points
- Zoom/scroll/pan: plain wheel zoom, Ctrl+wheel horizontal scroll, horizontal wheel/trackpad horizontal scroll, Alt+drag/middle-drag pan, F fit-to-view
- Resizable layer header panel: label/name column drag-resizable, fixed L/V/S control column stays pinned

**Signoff**:
- Live manual approval on 2026-05-22: plain wheel zoom, Ctrl+wheel horizontal scroll, Alt-drag pan, F fit-to-view, label resize, Add Effect, and Reset In/Out all verified.

**Exit Criteria**:
- Playback controls (play/pause/stop, fps display)
- Layer types: footage, solid, adjustment, null
- Solo/Mute/Lock per layer — DONE
- Keyboard shortcuts (Space=play, PageDown/Up=frame step, etc.)
- Split layer (Ctrl+Shift+D) — DONE
- Duplicate layer (Ctrl+D) — DONE
- Delete layer (Delete key) — DONE
- Zoom timeline horizontally (scroll wheel) — DONE
- Horizontal scroll via Ctrl+wheel / horizontal wheel / trackpad — DONE
- Layer bar context menu (delete, duplicate, split, properties) — DONE
- Playhead follow during playback — DONE
- Frame range from project settings — DONE

---

## P4: Effects + Properties (COMPLETE)

**Goal**: Effects stack panel per layer, properties panel for selected effect/layer.

**Started**: 2026-05-22 | **Completed**: 2026-05-22

**Dependencies**: P3 complete

**Results**:
- T043–T047: Effect insertion, wiring, adjustment rows, effects panel (all live-approved)
- T048: Save/reopen persistence via Boost XML serialization embedded in ProjectGuiSerialization
- T049: Duplicate effect-bearing layers and adjustment rows via Natron clipboard copy/paste
- T050: Split effect-bearing layers (clipboard copies effects, then trims both halves)
- T051: Adjustment row trim/split/move via disable-knob keyframes on all effects simultaneously

**T051 adjustment row trim semantics** (critical for future reference):
- Adjustment rows have no FrameRange knob — effects have no built-in time range.
- The disable knob's boolean animation IS the time range.
- On trim: `setAnimationEnabled(true)` on each effect's disable knob, clear old keyframes, then set:
  - Disabled at `startFrame - 1`
  - Enabled at `startFrame`
  - Disabled at `endFrame + 1`
- On move: same keyframes but shifted by new `timeOffset`
- On split: duplicate row, trim both halves, update keyframes on each
- On reset: clear all keyframes, set disable=false
- ALL effects in the row must be keyframed simultaneously on every change

**Exit Criteria**:
- Effects stack panel shows effects per layer
- Add/remove/reorder effects in stack
- Properties panel displays selected effect's parameters
- Parameters are editable and update rendering
- Reuse Natron's Knob system for parameter UI
- OpenFX plugins appear in effects list

---

## P5: Import/Export (COMPLETE)

**Goal**: Expose Read node settings per footage layer, add export/render panel.

**Dependencies**: P4 complete

**Started**: 2026-05-22
**Completed**: 2026-05-23

**Tasks**:
- T052: ✅ Right-click "Open Read Node" on footage layers
- T053: ✅ Flux Export/Render panel — reparents Write/Reformat NodeSettingsPanel with all knobs exposed (encoder options, codec, FPS, bitrate, OCIO, frame range, etc.), browse output path, Render button using Natron's standard render pipeline

---

## P6: Timeline Tree + Masks (COMPLETE)

**Goal**: Replace the separate effects-list workflow with an expandable timeline tree, then add layer/effect masks using Natron-native Roto/RotoPaint branches while preserving manual nodegraph edits.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P5 complete

**Started**: 2026-05-23

**Completed**: 2026-05-23

**UX Decision**:
- The timeline becomes the hierarchy for layers, effects, masks, and later keyframes.
- Selecting a layer row opens only the layer/gizmo properties.
- Selecting an effect row opens only that effect's properties.
- Selecting a mask row opens the Roto/RotoPaint properties and activates viewer tools.
- The old FluxEffectsPanel is phased out once effect rows work in the timeline.
- Precomp/manual branch nodes are preserved but not shown as layer children; a precomp icon appears on the layer row.

**Graph Rules**:
- Layer mask: `Read/Solid → Gizmo → Effects → [Unpremult] → Roto → Premult → Merge A`.
- Effect mask: `Reformat → Roto` side branch into the effect mask input.
- Effect mask branches do **not** count as precomp branches.
- Manual non-mask branches merging into the layer's main pipe count as precomp and must be preserved.

**Tasks**:
- T054: ✅ Timeline visible-row model — add `FluxVisibleRow`, expanded layer state, `yToRow()`, variable row heights; behavior initially identical to flat layer rows. Oracle-reviewed, build passed.
- T055: ✅ Timeline effect sub-rows — expanded layer shows main-pipe effects as indented children; selecting effect opens only that effect properties. Oracle-reviewed, build passed.
- T056: ✅ Move effect actions into timeline — add/remove/reorder effects from timeline context menus; start retiring FluxEffectsPanel. Oracle-reviewed, build passed.
- T057: ✅ FluxMask data model + serialization — `FluxMask`, `layer.masks`, `maskApplyNode`, `hasPrecompBranch`, project save/reopen. Oracle-reviewed, build passed.
- T058: ✅ Mask/branch discovery utilities — `discoverMaskInput(NodePtr)`, `isPremultNode(NodePtr)`, upstream branch classifier. Oracle-reviewed, build passed.
- T059: ✅ Timeline mask sub-rows and mask model actions — layer/effect mask model entries can be added from timeline context menus and appear as child rows; selecting a mask opens Roto/RotoPaint properties only when backing nodes already exist. No graph node creation or wiring yet. Oracle-reviewed, build passed.
- T060: ✅ Layer mask graph — create/connect mask apply in the layer main pipe with Reformat→Roto source; handle terminal Premult rule. Oracle-reviewed, build passed.
- T061: ✅ Effect mask graph — create/connect Reformat→Roto side branch into effect mask input. Oracle-reviewed, build passed.
- T062: ✅ Preserve manual layer branches — classify main pipe vs mask branches vs precomp branches; never destroy user-added nodes. Oracle-reviewed, build passed.
- T062A: ✅ P6 scrutinize blocker fixes — serialization versioning, temporary duplicate/split guards for masked/precomp layers, classifier source/path fixes, derived precomp-state reset, runtime artifact cleanup. Oracle-reviewed, build passed.
- T063: ✅ Duplicate/split full branch — branch-aware copy/paste for non-adjustment layers copies main pipe, mask branches, and precomp branches; restores FluxMask/FluxEffect references by old script name. Adjustment rows with masks remain disabled. Scrutinize-reviewed, build passed.
- T064: ✅ Correct layer-mask graph — removed unapproved Merge(in) layer-mask implementation; layer masks are now inline `source → [Unpremult] → Roto → Premult → Merge A`, with narrow old-artifact migration and stale-chain cleanup. Scrutinize-reviewed, build passed.
- T065: ✅ Roto/RotoPaint replace selected channels — added native `Zero selected input channels` checkbox, hidden internal `RotoReplaceChannels` preprocessing node, empty-Roto zeroing path, save/reopen crash fix, GL context fix for Shadertoy, and user CImg OFX discovery. Oracle-reviewed, build passed, CImg cache verified.

---

## P7: Shapes + Text

**Goal**: Shape and text layer types for motion graphics.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P6 complete

**Started**: 2026-05-24

**Prerequisite restoration work**:
- T069: ✅ Restore missing OFX provider coverage for bundled PyPlugs before implementing text/shape features that may depend on legacy native OpenFX providers. SeExpr/Text/Tile/Magick/ResolveMath providers are installed and validated; dependency audit reports 0 missing IDs; `lp_roughenEdges`, `lp_SimpleKeyer`, `Luma_to_Normals`, and `Vectors_Normalize` creation passes. Missing-plugin/library diagnostics Oracle-reviewed and validated with cold/warm cache broken-binary tests.
- T075: ⚠️ Groundwork only, not accepted product work — dedicated `openfx-flux/` source/build/deploy path and minimal `net.flux.openfx.TextRender` generator were proven only in narrow/headless contexts. This is revoked as completion evidence; see `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.
- T077: ⚠️ Rejected scaffold only, not accepted product work — `net.sf.openfx.FluxMotionText` wraps `TextRender -> FrameRange -> TimeOffset -> Grade -> Output`, but has no usable font picker, Add Animator UI, visible animator stack, range selectors, or per-element animation. This is revoked as completion evidence; see `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.

**Tasks**:
- T071: ✅ Text layer v1 — FluxText PyPlug and UI actions implemented. Current architecture is native Text → FrameRange → TimeOffset → Grade → Output, with Text controls promoted as group knobs in Natron PyPlug-exporter style and linked via `setAsAlias()`. Text controls appear first in properties, timeline trim uses FrameRange, opacity uses Grade multiply, and the viewer transform overlay registers against promoted Text knobs. Font selector changes sync `Text1name` to `Text1font`; promoted cascading choice metadata is preserved; 2D viewer overlay writes now create keyframes correctly. Build passed, `plugins/FluxText.py` compiles, deployed extras passed, Oracle reviewed overlay-keyframe fix, and live GUI validation passed for Text v1.
- T074: ✅ FluxTimeline animated property rows + grouped keyframes v1 — AE-like keyframe rows under layers/effects/masks, native Knob/Curve/Roto-backed animation, keyframe/inline-curve toggle, grouped multidim behavior without creating missing dimension keys, promoted Text1-only text keys, hidden adjustment disable keys, and aggregate Roto shape keyframes. Build passed, autonomous DopeSheet proof passed, and Nick manually validated final behavior.
- T075: 🧱 Flux-owned OFX text boundary spike — groundwork/prototype evidence only. Implementation/headless proof is not acceptance. See `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.
- T076: 🧱 openfx-flux deploy integration — groundwork/prototype evidence only. Deploy/discovery proof did not prove canonical user GUI runtime discovery of `net.flux.openfx.TextRender`. See recovery source of truth.
- T077: 🧱 FluxMotionText nodegroup scaffold — rejected scaffold only. Headless create/render/reload did not prove actual UI creation, viewer display, font UI, or text animator UX. See recovery source of truth.
- T078: 🧱 Real glyph rendering for Flux TextRender — renderer groundwork only. Glyph output was proven only in controlled/headless paths and does not prove FluxMotionText product behavior. See recovery source of truth.
- T079: ⛔ FluxMotionText layer integration — rejected/blocked. The real GUI path failed with `Failed to create TextRender1`, and the resulting UX lacks the required AE-style text animator system. See recovery source of truth.
- T080: ✅ FluxMotionText full UI Text Animators — core task manually accepted by Nick as working on 2026-05-26. Includes dynamic animator stacks, visible Animator panel, direct Add Animator action, selector shapes, range controls, per-character/word/line evaluation, playback/cache invalidation, Flux-owned TextRender path, and viewer zoom/proxy-stable TextRender output. Finishing touches are tracked separately in T081.
- T081: ✅ Text Animator finishing touches — build passed, Oracle-reviewed, and Nick manually validated all requested Natron GUI checks. Preserves target values during shape/keyframe workflow, adds selectable transform anchor dropdown, adds sliders for Scale/Start/End/Offset/Strength, and selecting animator rows opens/highlights the Animator panel.
- T072: ❌ Native Text justification/alignment follow-up cancelled — issue lives in external openfx-arena `Text.ofx.bundle`, while Flux text direction is the Flux-owned `TextRender`/FluxMotionText path.
- T073: ✅ Dope Sheet/keyframe readability polish — native Dope Sheet row/key painting readability improved and accepted as done.
- T082: ⏳ Adobe Illustrator import with layered PDF-preview/vector path — research online and locally in Natron/OpenFX first, then import `.ai` files that include PDF-compatible preview data, keep artwork resolution-independent at arbitrary render scale, and split Illustrator layers into separate Flux layers/nodes when the PDF/AI structure allows. Editable shape/text conversion is desirable but secondary to crisp vector-backed import.
- T083: ⏳ AI matte/mask generation and video depth tools — multi-model AI-assisted matte extraction/mask generation, with initial research candidates including SAM 3.1, MatAnyone/MatAnything-style video matting, BFRNet or similar helpers, and other current best video segmentation/matting models. Include video-stable depth estimation research; Depth Anything is not assumed good enough for video.

**Known follow-ups**:
- Native Text justification/alignment follow-up was dropped because it belongs to external openfx-arena Text, not the Flux-owned TextRender path.
- Dope Sheet/keyframe readability polish is complete for the native Dope Sheet paint path.
- Text Animator finishing touches from Nick's 2026-05-26 acceptance are complete and manually validated: value preservation during keyframe/shape changes, selectable animator transform anchor, sliders for main animator numeric controls, and animator-row selection opening/highlighting in the Animator panel.
- FluxMotionText/TextRender viewer zoom/proxy rendering is manually validated after fixing the layout/output-bounds split: layout uses scaled project/RoD bounds, output writes to the host destination bounds, and installed plugin hashes must match the build before GUI validation.
- Adobe Illustrator import should target AE-like behavior: use embedded PDF preview when available, preserve crisp logos/text/shapes at any scale, support full-file import and layered import, and investigate a dedicated vector render path if existing PDF/SVG readers rasterize too early. Research must include online sources and local Natron/OpenFX/OIIO/PDF/SVG tooling already present in this repo/runtime.
- AI-assisted matte/depth should be model-pluggable and video-oriented, not a one-model shortcut. Research must compare current online model options and local integration constraints before implementation.
- Linux installer/docs must stay path-agnostic: user-facing commands use `$FLUX_ROOT`, `$BUILD_DIR`, `$PLUGIN_PREFIX`, `$OFX_USER_PLUGIN_DIR`, `$HOME`, and `$XDG_CACHE_HOME`; installer end-to-end testing belongs in a clean VM/container or distrobox pod, not on the production host.

---

## P8: Polish + Cache

**Goal**: Improved cache, undo/redo, OCIO integration, performance optimization.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P7 complete

---

## Notes

- Phase durations are estimates. Actual time depends on what we discover during each phase.
- Each phase plan file is created when the previous phase nears completion.
- Task files are created as tasks are identified during planning.
- All dates are ISO 8601 format (YYYY-MM-DD).
