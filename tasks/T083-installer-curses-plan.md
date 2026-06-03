# Planner Report

## Status
ready

## Rationale
The reviewer blockers narrow the implementation to one coherent installer UX change: the public Bash installer must launch a single Python stdlib curses frontend, while Bash remains the private action/status dispatcher and the AI model manager is used only for concrete non-menu operations after curses-owned selections. The plan explicitly forbids both the old Bash ANSI pseudo-TUI fallback and the old `flux_model_manager.py interactive` / `remove-interactive` ANSI menus from any public installer AI path.

# Task Packet

## User Goal
Replace the broken flashing Bash pseudo-TUI installer with a real Python stdlib curses frontend. The public command remains exactly `tools/linux/flux-linux-setup.sh`, remains interactive-only with no public arguments, does not flash during navigation, exposes professional action descriptions/help/status including AI model/token/status/remove actions, preserves existing installer action logic where possible, and keeps the secure token policy unchanged: no plaintext persistence and no Hugging Face global login tools.

## Mode
general-coding

## Relevant Locations
- file: `tools/linux/flux-linux-setup.sh`
  symbol: `parse_args`
  approximate lines: 216-237
  stable anchor: `parse_args() {` and error text `Flux installer is interactive-only; run tools/linux/flux-linux-setup.sh with no arguments.`
  reason: Public entry validation currently rejects all arguments and enforces TTY; this must remain true for users while allowing only env-gated private dispatch.
  confidence: high
- file: `tools/linux/flux-linux-setup.sh`
  symbol: status helper functions
  approximate lines: 1060-1119
  stable anchor: `rpmfusion_status()`, `fedora_deps_status()`, `pyplug_status()`, `ofx_bundle_status()`, `ofx_cache_status()`, `print_tui_summary()`
  reason: Installer status for curses must be derived from existing Bash helper logic via a private status-summary action; Python must not reimplement these checks.
  confidence: high
- file: `tools/linux/flux-linux-setup.sh`
  symbol: existing Bash action functions
  approximate lines: 1120-1168 and surrounding earlier definitions
  stable anchor: `run_full_bootstrap()`, `run_deploy_runtime()`, `run_build_all()`, `run_checks()`, `launch_flux()`, `uninstall_flux()`
  reason: Preserve these functions as the implementation of setup/build/deploy/check/launch/uninstall actions; Python should invoke them through private dispatch rather than reimplementing installer logic.
  confidence: high
- file: `tools/linux/flux-linux-setup.sh`
  symbol: broken pseudo-TUI surface
  approximate lines: 1088-1327
  stable anchor: `print_tui_summary()`, `draw_tui_menu()`, `run_selected_tui_action()`, `run_tui()`, `main() { parse_args "$@"; refresh_derived_paths; run_tui; }`
  reason: Replace this Bash menu layer with Python curses launch and private dispatch, with no Bash pseudo-TUI fallback.
  confidence: high
- file: `tools/ai/flux_model_manager.py`
  symbol: concrete non-menu AI commands and old ANSI menu commands
  approximate lines: 1-388
  stable anchor: `cmd_list`, `cmd_status`, `cmd_install`, `cmd_remove`, `cmd_login`, `cmd_interactive`, `interactive_remove`, parser commands `interactive` / `remove-interactive`
  reason: The curses installer may use concrete model-manager commands such as `list`, `status`, `install <model-id>`, `remove <model-id>`, and `login --remember`; it must not dispatch to `interactive` or `remove-interactive` from public installer AI paths.
  confidence: high

## Allowed Edit Files
- `tools/linux/flux-linux-setup.sh`
- `tools/linux/flux_linux_setup_tui.py`
- `tools/ai/flux_model_manager.py`

## Read-Only Context Files
- `tasks/T083-installer-curses-plan.md`

## Required Change
1. In `tools/linux/flux-linux-setup.sh`, keep the public invocation contract unchanged: users run exactly `tools/linux/flux-linux-setup.sh`; public invocations with any argument must still fail as interactive-only; non-TTY public invocation must still fail.
2. Add a private internal dispatch boundary in `tools/linux/flux-linux-setup.sh`, gated by `FLUX_SETUP_INTERNAL_DISPATCH=1`, accepting only a private command shape like `__flux_setup_action <action-id> [action-args...]`. This private path must not be documented as user-facing.
3. Public arguments must remain rejected before private action handling unless both conditions are true: `FLUX_SETUP_INTERNAL_DISPATCH=1` and first argument is exactly `__flux_setup_action`. Without the env gate, `tools/linux/flux-linux-setup.sh __flux_setup_action ai-status` must fail with the public interactive-only/no-arguments error.
4. Add a private status-summary dispatch contract. Python curses must obtain installer status from Bash by running `FLUX_SETUP_INTERNAL_DISPATCH=1 tools/linux/flux-linux-setup.sh __flux_setup_action status-summary`. The output must be stable plain key/value or JSON data derived from the existing Bash status helper functions (`rpmfusion_status`, `fedora_deps_status`, `pyplug_status`, `ofx_bundle_status`, `ofx_cache_status`, binary/launcher/install checks, GPU/OpenGL/container hints). Do not reimplement this installer status logic in Python.
5. Map private action IDs to existing Bash logic only. Required IDs must cover at least: `full-bootstrap`, `deploy-runtime`, `build-all`, `configure`, `fedora-deps`, `rpmfusion`, `checks`, `launch`, `uninstall`, `status-summary`, and AI actions `ai-list`, `ai-status`, `ai-install <model-id>`, `ai-remove <model-id>`, `ai-token`.
6. Preserve confirmation behavior for mutating operations, either in Bash action wrappers or in the new curses UI. Do not silently perform mutating setup, build, deploy, dependency, cache, launch, uninstall, AI model install, AI model remove, or token persistence operations without confirmation.
7. Remove/stop using the Bash pseudo-TUI functions `draw_tui_menu`, `run_selected_tui_action`, and `run_tui` as the public menu implementation. Do not leave any Bash pseudo-TUI fallback. `print_tui_summary` may be removed or converted into private `status-summary` output, but must not be used as an ANSI menu renderer.
8. Add `tools/linux/flux_linux_setup_tui.py` as a Python 3 stdlib-only curses frontend. It must:
   - use `curses` for stable, non-flashing menu rendering;
   - show a stable layout with title, status/summary area populated from private `status-summary`, action list, selected action description/help, keyboard hints, and clear AI actions for install/download models, secure HF token entry, model status, and model removal;
   - support robust keys: Up/Down, Enter, q/Esc, Space for checklists, and tolerate terminal resize reasonably;
   - invoke shell actions only through `FLUX_SETUP_INTERNAL_DISPATCH=1 tools/linux/flux-linux-setup.sh __flux_setup_action ...`;
   - suspend/end curses while subprocesses own the terminal for logs, prompts, sudo, hidden token entry, model downloads/removals, and confirmations, then restore and redraw cleanly after the user returns;
   - avoid recording or echoing tokens and avoid passing tokens on command lines.
9. The Python curses frontend must own all visible public installer AI model selection/removal screens. It must include its own model install checklist and installed-model removal picker. Public installer AI paths must not dispatch to `flux_model_manager.py interactive` or `flux_model_manager.py remove-interactive`, and must not expose any secondary ANSI arrow pseudo-menu inside AI model install/remove paths.
10. Implement concrete non-menu AI dispatches after curses selection:
   - For model listing/checklist data, use a stable non-interactive command (`flux_model_manager.py list` plain output if sufficient, or add a narrow machine-readable `list --json` / equivalent if needed).
   - For model status, dispatch `ai-status` to existing model-manager status output or stable shell-wrapped status output.
   - For installing selected models, curses collects selected model IDs first, then dispatches concrete installs one by one, e.g. `__flux_setup_action ai-install <model-id>` wrapping `flux_model_manager.py install <model-id>` with existing confirmation/token semantics as appropriate.
   - For removing selected models, curses collects selected installed model IDs first, then dispatches concrete removes, e.g. `__flux_setup_action ai-remove <model-id>` wrapping `flux_model_manager.py remove <model-id>`.
   - For token entry, `ai-token` may temporarily suspend curses and call the existing hidden `getpass` path (`flux_model_manager.py login --remember` or equivalent). This hidden prompt is allowed; an arrow-key pseudo-menu is not.
11. In shell `main`, after public no-args/TTY validation and `refresh_derived_paths`, launch `python3 tools/linux/flux_linux_setup_tui.py` instead of Bash pseudo-TUI. Fail clearly if `python3` or Python `curses` is unavailable.
12. If `tools/ai/flux_model_manager.py` needs a machine-readable model list/status option to avoid fragile parsing, add only that narrow non-interactive option. Do not alter secure token policy, install/remove semantics, default model choices, or introduce new UI dependencies.

## Non-Goals
- Do not reimplement installer setup/build/deploy/uninstall action logic in Python.
- Do not reimplement Bash installer status logic in Python; use private `status-summary` dispatch.
- Do not make private dispatch public or document it as user-facing.
- Do not add public installer arguments.
- Do not keep a Bash pseudo-TUI fallback.
- Do not allow public installer AI actions to escape into `flux_model_manager.py interactive` or `remove-interactive`.
- Do not show any secondary ANSI pseudo-TUI inside AI model install/remove paths.
- Do not change AI model download semantics, secure token policy, or Hugging Face token storage behavior.
- Do not introduce Rust or non-stdlib Python UI dependencies.
- Do not perform broad installer refactors outside the allowed files.

## Validation
Commands:
- `bash -n tools/linux/flux-linux-setup.sh`
- `python3 -m py_compile tools/linux/flux_linux_setup_tui.py tools/ai/flux_model_manager.py`
- `tools/linux/flux-linux-setup.sh --help`
- `tools/linux/flux-linux-setup.sh __flux_setup_action ai-status`
- `printf '' | tools/linux/flux-linux-setup.sh`
- `FLUX_SETUP_INTERNAL_DISPATCH=1 tools/linux/flux-linux-setup.sh __flux_setup_action status-summary`
- `FLUX_SETUP_INTERNAL_DISPATCH=1 tools/linux/flux-linux-setup.sh __flux_setup_action ai-status`
- If machine-readable model list/status is added: run the new non-interactive model-manager command directly and through the shell private dispatch.
- Manual TTY validation: run `tools/linux/flux-linux-setup.sh`, navigate several items with Up/Down, confirm the layout does not flash or mix logs into the menu, verify the status panel comes from `status-summary`, open AI model install checklist, AI model removal picker, AI status, and token entry, run a harmless validation/status action, return to menu, then quit.

Expected result:
- Syntax/compile checks pass.
- Public argument invocation fails with the existing interactive-only/no-arguments policy.
- Ungated private-looking invocation `tools/linux/flux-linux-setup.sh __flux_setup_action ai-status` fails with the public no-args/interactive-only error.
- Non-TTY public invocation fails with the existing TTY-required policy.
- Private env-gated dispatch works for `status-summary` and `ai-status`.
- Curses UI is stable during navigation; action logs/prompts occupy the terminal only while curses is suspended; returning to the menu redraws cleanly.
- Installer AI install/remove selection is rendered only by the curses frontend, not by Bash ANSI menus and not by `flux_model_manager.py interactive` / `remove-interactive`.
- Selected AI models are installed/removed by concrete non-menu commands using explicit model IDs.
- Token prompts remain hidden/secure and no plaintext token persistence or Hugging Face global login is introduced.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- Python stdlib `curses` is unavailable in the supported target environment
- preserving current action logic requires changing AI token/model semantics
- implementing curses-owned AI selection would require calling `flux_model_manager.py interactive` or `remove-interactive`
- a secure token path would require plaintext persistence or Hugging Face global login tools

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
