# Planner Report

## Status
ready

## Rationale
This plan is intentionally narrow: it treats the observed `.pt` checkpoint/load failure as a provider-backend integration blocker, first requiring runtime API discovery inside the existing SAM3.1 provider environment, then allowing only the minimum manifest/runtime/probe edits needed to either use the real Meta SAM3.1 API or honestly report a verified blocker without fake masks.

# Task Packet

## User Goal
Make real SAM3.1 inference possible honestly for the installed `facebook/sam3.1` model directory that contains `sam3.1_multiplex.pt` plus config/tokenizer files but lacks `model.safetensors` / `pytorch_model.bin`. The current Transformers path in `tools/ai/sam31_real_inference_probe.py` cannot load it, and the provider env currently lacks the Meta `sam3` package.

## Mode
general-coding

## Relevant Locations
- file: `tools/ai/provider_runtime_manifest.json`
  symbol: `runtimes[0].packages` for runtime id `sam31`
  approximate lines: 7-24
  stable anchor: `"id": "sam31"` and `"packages": ["torch", "torchvision", "transformers"...`
  reason: Provider dependency source of truth; may need to add/pin Meta `sam3` install source if API discovery proves it is required.
  confidence: high
- file: `tools/ai/flux_provider_runtime.py`
  symbol: `cmd_install`
  approximate lines: 95-112
  stable anchor: `rest=[p for p in r.get("packages",[]) if p not in {"torch","torchvision"}]`
  reason: Runtime installer currently pip-installs non-torch packages from manifest; only edit if installing Meta `sam3` requires git package support or a different pip invocation.
  confidence: high
- file: `tools/ai/flux_provider_runtime.py`
  symbol: `package_import_status`
  approximate lines: 47-68
  stable anchor: `packages=[p for p in r.get("packages",[]) if not p.startswith("--")]`
  reason: If a git/VCS package or package alias is added, import-status derivation may need a narrow mapping so self-check reports `sam3` accurately.
  confidence: high
- file: `tools/ai/sam31_real_inference_probe.py`
  symbol: `self_check_payload`
  approximate lines: 59-97
  stable anchor: `"meta_sam3_package": import_version("sam3")["available"]`
  reason: Self-check already reports whether `sam3` is importable; may need to include discovered Meta API/module availability.
  confidence: high
- file: `tools/ai/sam31_real_inference_probe.py`
  symbol: `transformers_backend`
  approximate lines: 163-196
  stable anchor: `def transformers_backend(model_path: Path, device: str) -> dict[str, Any]:`
  reason: Current backend assumes Transformers can load HF weights; leave as a fallback only if verified, and do not force `.pt` through AutoModel without evidence.
  confidence: high
- file: `tools/ai/sam31_real_inference_probe.py`
  symbol: `run_real`
  approximate lines: 248-300
  stable anchor: `backend = transformers_backend(local_path, device)`
  reason: Backend selection currently tries Transformers first and only reports importable-but-not-implemented for `sam3`; worker should add a real Meta backend path here only after API inspection.
  confidence: high
- file: `tools/ai/sam31_real_inference_probe.py`
  symbol: `run_prompt`, `save_mask`, `extract_mask`
  approximate lines: 122-246
  stable anchor: `def run_prompt(backend: dict[str, Any], prompt_kind: str,...`
  reason: Existing prompt execution and mask assertions can be reused or extended for a Meta backend, but must keep honest unsupported/blocked statuses.
  confidence: high

## Allowed Edit Files
- `tools/ai/provider_runtime_manifest.json`
- `tools/ai/flux_provider_runtime.py`
- `tools/ai/sam31_real_inference_probe.py`

## Read-Only Context Files
- `AGENTS.md`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`
- `/home/npittas/.pi/agent/COLLABORATION.md`

## Required Change
1. In the existing SAM3.1 provider env, perform and record a narrow API discovery before coding the backend adapter:
   - Use `tools/ai/flux_provider_runtime.py python sam31` to get the provider Python.
   - Confirm current imports and model path with the probe self-check.
   - If `sam3` is not importable, install the real Meta SAM3 package into the provider env using the smallest explicit source supported by Meta documentation/repository evidence, preferably a pinned git URL/commit if no released wheel exists. Do not install unrelated research packages.
   - Inspect importable modules/callables with provider Python (`import sam3`, `pkgutil.walk_packages`, `inspect.signature`, `help`/source where available) to identify the documented or discoverable API for loading local `sam3.1_multiplex.pt` and running image prompts.
   - Separately verify whether Transformers has any legitimate local `.pt` loading path for this repo/config; only keep/use Transformers if this is proven by an actual successful local load or documented API.
2. Update `tools/ai/provider_runtime_manifest.json` only if the Meta package is required and a reproducible install spec is identified.
3. Update `tools/ai/flux_provider_runtime.py` only if needed to support that reproducible install spec or to make runtime import-status checks correctly recognize `sam3` for non-standard package specs.
4. Update `tools/ai/sam31_real_inference_probe.py` to add a real backend adapter for the verified API only:
   - Load the local model directory/checkpoint without downloading weights.
   - Select CUDA/CPU through the existing `choose_device` behavior.
   - Run the required still-image prompts only where the verified API supports them: text, box, and point.
   - Save real masks through `save_mask`; preserve nonzero/file-size assertions.
   - Mark unsupported prompt modalities as `unsupported`/`blocked` with explicit runtime blockers rather than fabricating output.
   - Preserve the existing honest nonzero exit behavior (`3` no backend/load blocker, `4` prompt proof failed/unsupported) unless a compelling local reason requires a narrow adjustment.
5. Keep the change entirely within provider runtime/probe wiring. No GUI, installer docs, global task status, model-manager policy, or unrelated AI-panel edits.

## Non-Goals
- Do not fake masks, synthesize placeholder PNGs, or mark unsupported prompts as succeeded.
- Do not edit GUI/installer/docs/task status files.
- Do not change model download/storage policy or add plaintext tokens.
- Do not implement video propagation, mask-paint prompts, or AI-panel UX in this task.
- Do not replace the installed model with a different checkpoint unless the verified Meta API proves the current `sam3.1_multiplex.pt` is incompatible and the task stops for orchestrator/user decision.

## Validation
Commands:
- `python3 tools/ai/flux_provider_runtime.py status sam31 --json`
- `python3 tools/ai/flux_provider_runtime.py self-check sam31 --json`
- `$(python3 tools/ai/flux_provider_runtime.py python sam31) - <<'PY'
import importlib, inspect, json, pkgutil
mods = {}
for name in ('sam3', 'torch', 'transformers'):
    try:
        m = importlib.import_module(name)
        mods[name] = {'ok': True, 'file': getattr(m, '__file__', None), 'version': getattr(m, '__version__', None)}
    except Exception as e:
        mods[name] = {'ok': False, 'error': type(e).__name__ + ': ' + str(e)}
print(json.dumps(mods, indent=2, sort_keys=True))
PY`
- Run the real PPM proof command used for the failed proof, with the provider Python and the same image/output-dir/prompt arguments, e.g. `$(python3 tools/ai/flux_provider_runtime.py python sam31) tools/ai/sam31_real_inference_probe.py --json --image tools/ai/proof_assets/sam31_probe_source.ppm --output-dir <new-output-dir> --device auto`.
- Inspect `<new-output-dir>/sam31_real_inference_result.json` and the produced `mask_text.png`, `mask_box.png`, `mask_point.png`.

Expected result:
- Provider status/self-check either reports ready with `sam3` importable or reports a precise package/API blocker.
- API inspection output identifies the exact `sam3` callable/module path used, or proves no usable API exists.
- Successful proof requires result JSON backend name/kind to identify the real backend, all required proof entries (`text`, `box`, `point`) to have `status: "succeeded"`, and all three mask files to exist, be non-empty, match source image dimensions after `save_mask`, and have `nonzero_pixels > 0`.
- If the Meta package/API cannot load the local `.pt` or does not support required prompts, the probe must exit non-zero with a clear blocker and no fake success.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- Meta `sam3` package cannot be installed reproducibly into the provider env
- installed/imported Meta APIs do not support loading the local `sam3.1_multiplex.pt`
- verified API supports only a different model/checkpoint or different prompt modality than the installed `sam3.1_multiplex.pt`
- Transformers has no verified `.pt` loading path and Meta API discovery is inconclusive
- CUDA/provider dependency conflicts require broad runtime policy changes
- any prompt proof would require placeholder or synthetic mask output

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, exact Meta/Transformers API evidence found, validation evidence, blockers, and task-specific risks.
