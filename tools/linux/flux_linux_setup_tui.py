#!/usr/bin/env python3
"""Stdlib curses frontend for the Flux Linux setup wrapper."""
from __future__ import annotations
import curses, json, os, subprocess, sys
from pathlib import Path

SCRIPT = Path(__file__).with_name("flux-linux-setup.sh")
ENV = dict(os.environ, FLUX_SETUP_INTERNAL_DISPATCH="1")

ACTIONS = [
    # Artifact-based Installation and Validation
    ("full-bootstrap", "[Artifact] Full setup from runtime artifact", "Install runtime deps, extract prebuilt Flux, write launchers, and validate.", True),
    ("install-artifact", "[Artifact] Install selected runtime artifact", "Extract a local/remote prebuilt Flux runtime artifact and write launchers.", True),
    ("runtime-deps", "[Artifact] Install runtime dependencies (Fedora)", "Install Fedora packages required to run a prebuilt Flux artifact.", True),
    ("artifact-checks", "[Artifact] Validate artifact installation", "Run runtime dependency, PyPlug, OFX, and install validation checks.", False),

    # AI Model and Runtime Management
    ("ai-install-menu", "[AI Tools] Install or update AI models", "Choose models to install or update via curses checklist.", False),
    ("ai-runtime-install-menu", "[AI Tools] Install or update AI runtimes", "Choose provider runtimes to repair or create isolated venvs.", False),
    ("ai-token", "[AI Tools] Hugging Face token", "Set or update secure Hugging Face API token.", True),
    ("ai-status", "[AI Tools] AI model status", "Show models, configuration paths, and cache status.", False),
    ("ai-runtime-status-menu", "[AI Tools] AI runtime status / self-check", "Check status or run diagnostic self-checks on runtimes.", False),
    ("ai-remove-menu", "[AI Tools] Remove installed AI model", "Remove installed models by explicit model ID.", False),

    # Common Utilities
    ("launch", "Launch Flux", "Start the installed launcher or binary.", True),
    ("uninstall", "Uninstall Flux", "Remove files recorded in the install manifest.", True),

    # Developer-only Build and Staging Actions
    ("deploy-runtime", "[Developer] Repair runtime from build tree", "Repair installed app, plugins, launcher from local build outputs.", True),
    ("source-bootstrap", "[Developer] Build/install from source", "Install build deps, configure, build Flux from source, and validate.", True),
    ("build-all", "[Developer] Compile Flux from source", "Compile Flux/Natron and Flux OFX targets from build tree.", True),
    ("configure", "[Developer] Configure CMake build", "Run CMake configuration for current source/build settings.", True),
    ("fedora-deps", "[Developer] Install build dependencies (Fedora)", "Install Fedora packages required to compile Flux from source.", True),
    ("rpmfusion", "[Developer] Enable RPM Fusion", "Enable RPM Fusion free repo for FFmpeg-related packages.", True),
    ("checks", "[Developer] Validate build tree and installation", "Run full dependency, build, PyPlug, OFX, and cache checks.", False),
    ("ai-host-prereqs-install", "[Developer] Install AI build packages (Fedora)", "Install git, pip, cargo, and rust for building models.", True),
    ("ai-user-tools-install", "[Developer] Install user tools (uv, bun)", "Install uv and bun into user environment.", True),
    ("corridorkey-builder-prereqs", "[Developer] Install CorridorKey builder toolkit", "Install build packages, uv/bun, and TensorRT staging.", True),
    ("cuda-toolkit-install", "[Developer] Install CUDA toolkit (Fedora)", "Install CUDA toolkit required to compile CorridorKey engine.", True),
    ("installer-self-test", "[Developer] Installer self-test", "Fresh-clone configure and submodule repair verification.", False),
]

def call(action, *args, capture=False):
    cmd = [str(SCRIPT), "__flux_setup_action", action, *args]
    if capture:
        return subprocess.run(cmd, env=ENV, text=True, capture_output=True)
    return subprocess.run(cmd, env=ENV)

def status_rows():
    p = call("status-summary", capture=True)
    rows = []
    for line in p.stdout.splitlines():
        if "=" in line:
            k, v = line.split("=", 1); rows.append((k.replace("_", " "), v))
    if p.returncode != 0:
        rows.append(("status", "unavailable; run validation for details"))
    return rows

def model_rows(installed_only=False):
    args = ["--include-unimplemented"] if installed_only else []
    p = call("ai-list", *args, capture=True)
    if p.returncode != 0:
        return [], p.stderr or p.stdout or "AI model list unavailable. Install runtime artifact or dependencies first."
    try:
        data = json.loads(p.stdout)
    except json.JSONDecodeError as e:
        return [], f"AI model list parse failed: {e}"
    rows = data.get("models", [])
    if installed_only:
        rows = [m for m in rows if m.get("installed")]
    return rows, ""

def runtime_rows():
    p = call("ai-runtime-list", capture=True)
    if p.returncode != 0:
        return [], p.stderr or p.stdout or "AI runtime list unavailable."
    try:
        data = json.loads(p.stdout)
    except json.JSONDecodeError as e:
        return [], f"AI runtime list parse failed: {e}"
    return data.get("runtimes", []), ""

def draw(stdscr, selected, msg=""):
    h, w = stdscr.getmaxyx(); stdscr.erase(); y = 0
    stdscr.addnstr(y, 0, "Flux Linux Setup", w - 1, curses.A_BOLD); y += 1
    stdscr.addnstr(y, 0, "Status (from private Bash status-summary)", w - 1, curses.A_UNDERLINE); y += 1
    for k, v in status_rows()[: max(3, min(10, h // 3))]:
        stdscr.addnstr(y, 0, f"{k:16} {v}", w - 1); y += 1
    y += 1; stdscr.addnstr(y, 0, "Actions", w - 1, curses.A_UNDERLINE); y += 1
    list_h = max(1, h - y - 6); start = max(0, selected - list_h + 1)
    for i, (_, title, _, _) in enumerate(ACTIONS[start:start+list_h], start):
        attr = curses.A_REVERSE if i == selected else curses.A_NORMAL
        stdscr.addnstr(y, 0, f"{'>' if i == selected else ' '} {title}", w - 1, attr); y += 1
    _, title, help_text, _ = ACTIONS[selected]
    stdscr.addnstr(h-5, 0, f"Selected: {title}", w - 1, curses.A_BOLD)
    stdscr.addnstr(h-4, 0, help_text, w - 1)
    stdscr.addnstr(h-3, 0, msg, w - 1)
    stdscr.addnstr(h-2, 0, "Keys: Up/Down move  Enter run  q/Esc quit", w - 1, curses.A_DIM)
    stdscr.refresh()

def text_prompt(stdscr, prompt):
    curses.echo()
    h, w = stdscr.getmaxyx()
    stdscr.erase()
    stdscr.addnstr(0, 0, prompt, w-1, curses.A_BOLD)
    stdscr.addnstr(2, 0, "Artifact path or URL: ", w-1)
    stdscr.refresh()
    value = stdscr.getstr(2, min(22, w-1), 4096).decode("utf-8", errors="replace").strip()
    curses.noecho()
    return value

def confirm(stdscr, text):
    stdscr.addnstr(curses.LINES-3, 0, text + "  y/N", curses.COLS-1, curses.A_BOLD); stdscr.clrtoeol(); stdscr.refresh()
    return stdscr.getch() in (ord('y'), ord('Y'))

def suspend_run(stdscr, action, *args):
    curses.def_prog_mode(); curses.endwin()
    try:
        rc = call(action, *args).returncode
        input("\nPress Enter to return to the Flux installer.")
    finally:
        curses.reset_prog_mode(); stdscr.keypad(True); curses.curs_set(0)
    return rc

def checklist(stdscr, title, rows, installed_remove=False):
    if not rows:
        return []
    cur = 0; checked = {i for i, m in enumerate(rows) if (m.get("default_installable") and m.get("default_enabled") and not installed_remove)}
    while True:
        h, w = stdscr.getmaxyx(); stdscr.erase(); stdscr.addnstr(0, 0, title, w-1, curses.A_BOLD)
        stdscr.addnstr(1, 0, "Up/Down move  Space toggle  Enter continue  q/Esc cancel", w-1, curses.A_DIM)
        start = max(0, cur - (h-5) + 1)
        for y, i in enumerate(range(start, min(len(rows), start+h-5)), 3):
            m = rows[i]; mark = "[x]" if i in checked else "[ ]"; state = "installed" if m.get("installed") else "missing"
            labels = ", ".join(m.get("labels") or [])
            text = f"{mark} {m['id']} — {m.get('display_name','')} ({state}) {labels}"
            stdscr.addnstr(y, 0, text, w-1, curses.A_REVERSE if i == cur else curses.A_NORMAL)
        stdscr.refresh(); ch = stdscr.getch()
        if ch in (ord('q'), 27): return []
        if ch in (curses.KEY_UP, ord('k')): cur = max(0, cur-1)
        elif ch in (curses.KEY_DOWN, ord('j')): cur = min(len(rows)-1, cur+1)
        elif ch == ord(' '): checked.symmetric_difference_update({cur})
        elif ch in (10, 13): return [rows[i]["id"] for i in sorted(checked)]

def pick_one(stdscr, title, rows):
    if not rows:
        return None
    cur = 0
    while True:
        h, w = stdscr.getmaxyx(); stdscr.erase(); stdscr.addnstr(0, 0, title, w-1, curses.A_BOLD)
        stdscr.addnstr(1, 0, "Up/Down move  Enter choose  q/Esc cancel", w-1, curses.A_DIM)
        start = max(0, cur - (h-5) + 1)
        for y, i in enumerate(range(start, min(len(rows), start+h-5)), 3):
            row = rows[i]
            state = "installed" if row.get("installed") else "missing"
            models = ", ".join(row.get("models", [])) or "no models"
            text = f"{row['id']} — {row.get('display_name','')} ({state}) models: {models}"
            stdscr.addnstr(y, 0, text, w-1, curses.A_REVERSE if i == cur else curses.A_NORMAL)
        stdscr.refresh(); ch = stdscr.getch()
        if ch in (ord('q'), 27): return None
        if ch in (curses.KEY_UP, ord('k')): cur = max(0, cur-1)
        elif ch in (curses.KEY_DOWN, ord('j')): cur = min(len(rows)-1, cur+1)
        elif ch in (10, 13): return rows[cur]



def runtime_row(r):
    models = ", ".join(r.get("models", [])) or "no models"
    state = "installed" if r.get("installed") else "missing"
    return {"id": r["id"], "display_name": r.get("display_name", r["id"]), "installed": r.get("installed", False), "models": r.get("models", []), "title": f"{r['id']} — {r.get('display_name', r['id'])} ({state})", "help": f"models: {models}", "value": r}
def main(stdscr):
    curses.curs_set(0); stdscr.keypad(True); selected = 0; msg = ""
    while True:
        draw(stdscr, selected, msg); ch = stdscr.getch(); msg = ""
        if ch in (ord('q'), 27): return 0
        if ch in (curses.KEY_UP, ord('k')): selected = max(0, selected-1); continue
        if ch in (curses.KEY_DOWN, ord('j')): selected = min(len(ACTIONS)-1, selected+1); continue
        if ch not in (10, 13): continue
        action, title, _, mutating = ACTIONS[selected]
        if action == "ai-install-menu":
            rows, err = model_rows(False); msg = err
            ids = checklist(stdscr, "Install/update Flux AI models", rows)
            for mid in ids:
                if confirm(stdscr, f"Install/update {mid}?"):
                    suspend_run(stdscr, "ai-install", mid)
        elif action == "ai-runtime-install-menu":
            rows, err = runtime_rows(); msg = err
            ids = checklist(stdscr, "Install/update Flux AI runtimes", rows)
            for rid in ids:
                if confirm(stdscr, f"Install/update runtime {rid}?"):
                    suspend_run(stdscr, "ai-runtime-install", rid)
        elif action == "ai-runtime-status-menu":
            rows, err = runtime_rows(); msg = err
            runtime = pick_one(stdscr, "Flux AI runtimes", [runtime_row(r) for r in rows])
            if runtime:
                rid = runtime["id"]
                if confirm(stdscr, f"Run self-check for {rid}? (No = status only)"):
                    suspend_run(stdscr, "ai-runtime-self-check", rid)
                else:
                    suspend_run(stdscr, "ai-runtime-status", rid)
        elif action == "ai-remove-menu":
            rows, err = model_rows(True); msg = err or ("No installed AI models." if not rows else "")
            ids = checklist(stdscr, "Remove installed Flux AI models", rows, True)
            for mid in ids:
                if confirm(stdscr, f"Remove {mid}?"):
                    suspend_run(stdscr, "ai-remove", mid)
        elif action in ("full-bootstrap", "install-artifact"):
            artifact = os.environ.get("FLUX_RUNTIME_ARTIFACT", "")
            if not artifact:
                artifact = text_prompt(stdscr, title)
            if artifact and confirm(stdscr, f"Run {title} using {artifact}?"):
                suspend_run(stdscr, action, artifact)
            elif not artifact:
                msg = "Artifact path or URL is required."
        else:
            if (not mutating) or confirm(stdscr, f"Run {title}?"):
                suspend_run(stdscr, action)

if __name__ == "__main__":
    raise SystemExit(curses.wrapper(main))
