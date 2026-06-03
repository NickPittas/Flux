# Planner Report

## Status
ready

## Rationale
This plan is sufficient and narrow because the provider runtime and self-check are already ready, the proof probe has a single command-line entry point, and any necessary implementation work is constrained to adapting the probe to the real local SAM3.1 API without fabricating masks or touching GUI/install/task-status files.

# Task Packet

## User Goal
Run a real SAM3.1 text/box/point inference proof through the provider runtime, first on the existing PPM proof asset and then optionally on one extracted PNG frame from Nick's MOV. If the run fails only because the local SAM3.1 API shape differs from the current probe assumptions, minimally fix `tools/ai/sam31_real_inference_probe.py` only. Do not fake output.

## Mode
general-coding

## Relevant Locations
- file: `tools/ai/sam31_real_inference_probe.py`
  symbol: `run_real`, `transformers_backend`, `run_prompt`, `extract_mask`, `save_mask`, `build_parser`
  approximate lines: 157-342
  stable anchor: `RESULT_NAME = "sam31_real_inference_result.json"`
  reason: Probe entry point, backend selection, prompt invocation, mask extraction/saving, CLI defaults.
  confidence: high
- file: `tools/ai/flux_provider_runtime.py`
  symbol: `cmd_python`, `cmd_self`, `status_payload`
  approximate lines: 61-155
  stable anchor: `def cmd_python(args):`
  reason: Provider-env Python command and runtime readiness context; use as read-only command source.
  confidence: high
- file: `tools/ai/proof_assets/sam31_probe_source.ppm`
  symbol: n/a
  approximate lines: n/a
  stable anchor: existing proof asset path
  reason: First required real-inference proof input.
  confidence: high

## Allowed Edit Files
- `tools/ai/sam31_real_inference_probe.py`

## Read-Only Context Files
- `tools/ai/flux_provider_runtime.py`
- `tools/ai/proof_assets/sam31_probe_source.ppm`
- `/home/npittas/Videos/For_Test/Video_For_Test.mov`

## Required Change
1. Do not edit first. From `/home/npittas/Flux`, resolve provider Python and run the self-check through the provider env:
   ```bash
   cd /home/npittas/Flux
   SAM31_PY="$(python3 tools/ai/flux_provider_runtime.py python sam31)"
   "$SAM31_PY" tools/ai/sam31_real_inference_probe.py --self-check --json
   ```
2. Run the real proof on the existing PPM asset:
   ```bash
   cd /home/npittas/Flux
   SAM31_PY="$(python3 tools/ai/flux_provider_runtime.py python sam31)"
   rm -rf /tmp/flux-sam31-proof-ppm
   mkdir -p /tmp/flux-sam31-proof-ppm
   "$SAM31_PY" tools/ai/sam31_real_inference_probe.py \
     --json \
     --device auto \
     --image tools/ai/proof_assets/sam31_probe_source.ppm \
     --output-dir /tmp/flux-sam31-proof-ppm \
     --text "red rectangle" \
     --box 100,90,300,350 \
     --point 200,220,1
   ```
3. Inspect `/tmp/flux-sam31-proof-ppm/sam31_real_inference_result.json` and assert honestly:
   - JSON schema is `org.flux.ai.sam31-real-inference-result.v1`.
   - `device.selected` is present (`cuda` preferred if available, `cpu` acceptable only if runtime chose it).
   - `backend.kind` is a real detected backend, not `none`.
   - `proofs.text.status`, `proofs.box.status`, and `proofs.point.status` are all `succeeded`.
   - `mask_text.png`, `mask_box.png`, and `mask_point.png` exist, are non-empty, and each proof reports `nonzero_pixels > 0`.
4. If and only if the PPM proof fails due a concrete SAM3.1 API/model mismatch in the probe, minimally edit `tools/ai/sam31_real_inference_probe.py` to call the actually installed local API and/or extract its returned mask format. Keep the same CLI, JSON result contract, nonzero mask assertions, and no fake/synthetic masks. Re-run steps 1-3 after the fix.
5. Extract one PNG frame from Nick's MOV for optional second proof:
   ```bash
   cd /home/npittas/Flux
   mkdir -p /tmp/flux-sam31-proof-mov
   ffmpeg -y -i /home/npittas/Videos/For_Test/Video_For_Test.mov \
     -vf "select=eq(n\,24)" -frames:v 1 \
     /tmp/flux-sam31-proof-mov/frame_000024.png
   ```
6. Inspect `/tmp/flux-sam31-proof-mov/frame_000024.png` visually before choosing prompt geometry. Then run the optional MOV-frame proof with adjusted prompt/coords; placeholders below must be replaced based on the inspected frame:
   ```bash
   cd /home/npittas/Flux
   SAM31_PY="$(python3 tools/ai/flux_provider_runtime.py python sam31)"
   "$SAM31_PY" tools/ai/sam31_real_inference_probe.py \
     --json \
     --device auto \
     --image /tmp/flux-sam31-proof-mov/frame_000024.png \
     --output-dir /tmp/flux-sam31-proof-mov \
     --text "<VISIBLE_OBJECT_DESCRIPTION_FROM_FRAME>" \
     --box <X1,Y1,X2,Y2_AROUND_VISIBLE_OBJECT> \
     --point <X,Y,1_INSIDE_VISIBLE_OBJECT>
   ```
7. Record the exact commands run, exit codes, result JSON paths, mask paths, and whether each proof succeeded or was blocked by a real runtime/API/model issue.

## Non-Goals
- Do not edit GUI files, installer files, manifests, model manager files, docs, or task-status files.
- Do not mark `tasks/TASKS.md` or `plans/PHASES.md` complete.
- Do not download models, change provider runtime installation, or alter environment setup.
- Do not add fake outputs, placeholder masks, random masks, or success overrides.
- Do not broaden into video propagation, mask-paint prompts, UI integration, or model-management UX.

## Validation
Commands:
- `cd /home/npittas/Flux && SAM31_PY="$(python3 tools/ai/flux_provider_runtime.py python sam31)" && "$SAM31_PY" tools/ai/sam31_real_inference_probe.py --self-check --json`
- `cd /home/npittas/Flux && SAM31_PY="$(python3 tools/ai/flux_provider_runtime.py python sam31)" && rm -rf /tmp/flux-sam31-proof-ppm && mkdir -p /tmp/flux-sam31-proof-ppm && "$SAM31_PY" tools/ai/sam31_real_inference_probe.py --json --device auto --image tools/ai/proof_assets/sam31_probe_source.ppm --output-dir /tmp/flux-sam31-proof-ppm --text "red rectangle" --box 100,90,300,350 --point 200,220,1`
- `test -s /tmp/flux-sam31-proof-ppm/sam31_real_inference_result.json && test -s /tmp/flux-sam31-proof-ppm/mask_text.png && test -s /tmp/flux-sam31-proof-ppm/mask_box.png && test -s /tmp/flux-sam31-proof-ppm/mask_point.png`
- `cd /home/npittas/Flux && mkdir -p /tmp/flux-sam31-proof-mov && ffmpeg -y -i /home/npittas/Videos/For_Test/Video_For_Test.mov -vf "select=eq(n\,24)" -frames:v 1 /tmp/flux-sam31-proof-mov/frame_000024.png`
- Optional after frame inspection: run the MOV-frame proof command from Required Change step 6 with real prompt and coordinates.

Expected result:
- Self-check exits 0 and reports `ready: true`.
- PPM proof exits 0 and writes `/tmp/flux-sam31-proof-ppm/sam31_real_inference_result.json` plus three non-empty mask PNGs with nonzero pixels.
- Optional MOV-frame extraction writes `/tmp/flux-sam31-proof-mov/frame_000024.png`; optional proof exits 0 and writes result JSON plus three non-empty mask PNGs after prompt/coords are adjusted to the frame.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- self-check is no longer ready through provider env
- model weights are missing, corrupt, gated, or incompatible with local offline loading
- CUDA/PyTorch/provider runtime fails independently of probe code
- detected backend does not expose real text, box, and point prompt inference
- SAM3.1 API requires a product/API choice beyond a minimal adapter in `sam31_real_inference_probe.py`
- returned outputs do not contain real mask tensors/arrays/images that can be validated as non-empty
- only possible path would be fake/synthetic masks or changing success criteria
- MOV frame prompt/coords cannot be chosen without visual inspection

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
