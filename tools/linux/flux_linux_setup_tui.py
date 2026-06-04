#!/usr/bin/env python3
"""Stdlib curses frontend for the Flux Linux setup wrapper."""
from __future__ import annotations
import curses, json, os, subprocess, sys
from pathlib import Path

SCRIPT = Path(__file__).with_name("flux-linux-setup.sh")
ENV = dict(os.environ, FLUX_SETUP_INTERNAL_DISPATCH="1")

ACTIONS = [
    ("full-bootstrap", "Full setup / update Flux", "Build, install app/runtime/plugins/launcher, validate, and offer default AI setup.", True),
    ("ai-install-menu", "Install or update AI models", "Choose models in this curses checklist, then run concrete model installs.", False),
    ("ai-token", "Hugging Face token", "Hidden token prompt; optional persistence only in accepted secure keyring.", True),
    ("ai-status", "AI model status", "Show model/cache/config paths and installed/missing model state.", False),
    ("ai-runtime-install", "Install SAM3.1 runtime", "Create/update isolated provider venv under Flux/ai-envs/sam31; never uses Flux/Plugins/python.", True),
    ("ai-runtime-status", "SAM3.1 runtime status", "Show isolated provider runtime env/model readiness.", False),
    ("ai-runtime-self-check", "SAM3.1 runtime self-check", "Run the SAM3.1 probe through the provider env Python.", False),
    ("ai-remove-menu", "Remove installed AI model", "Choose installed models in this curses picker, then remove by explicit model id.", False),
    ("deploy-runtime", "Install/repair runtime only", "Repair installed app, Python runtime, plugins, launcher; no rebuild.", True),
    ("installer-self-test", "Installer self-test", "Fresh-clone configure and stale-submodule repair verification without host install mutation.", False),
    ("build-all", "Build Flux from source", "Compile Flux/Natron and Flux OFX targets from the configured build tree.", True),
    ("configure", "Configure build", "Run CMake configuration for current source/build settings.", True),
    ("fedora-deps", "Install Fedora dependencies", "Install Fedora build/runtime packages with sudo dnf.", True),
    ("rpmfusion", "Enable RPM Fusion", "Enable RPM Fusion free for FFmpeg-related Fedora packages.", True),
    ("checks", "Validate installation", "Run dependency, build, PyPlug, OFX, cache, and install checks.", False),
    ("launch", "Launch Flux", "Start the installed launcher or build-tree Flux binary.", True),
    ("uninstall", "Uninstall Flux", "Remove files recorded in the Flux install manifest.", True),
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
    p = call("ai-list", capture=True)
    if p.returncode != 0:
        return [], p.stderr or p.stdout or "AI model list unavailable. Deploy runtime/AI tools first."
    try:
        data = json.loads(p.stdout)
    except json.JSONDecodeError as e:
        return [], f"AI model list parse failed: {e}"
    rows = data.get("models", [])
    if installed_only:
        rows = [m for m in rows if m.get("installed")]
    return rows, ""

def draw(stdscr, selected, msg=""):
    h, w = stdscr.getmaxyx(); stdscr.erase(); y = 0
    stdscr.addnstr(y, 0, "Flux Linux Setup", w - 1, curses.A_BOLD); y += 1
    stdscr.addnstr(y, 0, "Status (from private Bash status-summary)", w - 1, curses.A_UNDERLINE); y += 1
    for k, v in status_rows()[: max(3, min(10, h // 3))]:
        stdscr.addnstr(y, 0, f"{k:16} {v}", w - 1); y += 1
    y += 1; stdscr.addnstr(y, 0, "Actions", w - 1, curses.A_UNDERLINE); y += 1
    list_top = y; list_h = max(1, h - y - 6); start = max(0, selected - list_h + 1)
    for i, (_, title, _, _) in enumerate(ACTIONS[start:start+list_h], start):
        attr = curses.A_REVERSE if i == selected else curses.A_NORMAL
        stdscr.addnstr(y, 0, f"{'>' if i == selected else ' '} {title}", w - 1, attr); y += 1
    _, title, help_text, _ = ACTIONS[selected]
    stdscr.addnstr(h-5, 0, f"Selected: {title}", w - 1, curses.A_BOLD)
    stdscr.addnstr(h-4, 0, help_text, w - 1)
    stdscr.addnstr(h-3, 0, msg, w - 1)
    stdscr.addnstr(h-2, 0, "Keys: Up/Down move  Enter run  q/Esc quit", w - 1, curses.A_DIM)
    stdscr.refresh()

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
        elif action == "ai-remove-menu":
            rows, err = model_rows(True); msg = err or ("No installed AI models." if not rows else "")
            ids = checklist(stdscr, "Remove installed Flux AI models", rows, True)
            for mid in ids:
                if confirm(stdscr, f"Remove {mid}?"):
                    suspend_run(stdscr, "ai-remove", mid)
        else:
            if (not mutating) or confirm(stdscr, f"Run {title}?"):
                suspend_run(stdscr, action)

if __name__ == "__main__":
    raise SystemExit(curses.wrapper(main))
