#!/usr/bin/env python3
"""Flux graphical installer/repair/troubleshooter.

Run directly from a fresh checkout with:
    tools/linux/flux-linux-setup.sh
"""
from __future__ import annotations

import json
import os
import queue
import subprocess
import signal
import sys
import threading
from pathlib import Path
from tkinter import BOTH, DISABLED, END, LEFT, NORMAL, TOP, W, X, Y, BooleanVar, Tk, messagebox
from tkinter import scrolledtext, ttk

SCRIPT_DIR = Path(__file__).resolve().parent
SETUP_SCRIPT = SCRIPT_DIR / "flux-linux-setup.sh"
FLUX_ROOT = SCRIPT_DIR.parent.parent

MUTATING_ACTIONS = {
    "full-bootstrap",
    "deploy-runtime",
    "fedora-deps",
    "rpmfusion",
    "uninstall",
    "ai-install",
    "ai-remove",
    "ai-runtime-install",
}

REPAIR_ACTIONS = {
    "Install/repair the app, bundled Python tools, PyPlugs, OFX bundles, launcher, and OFX cache": ("deploy-runtime", ()),
    "Build Flux and bundled OFX plugins": ("build-all", ()),
    "Run installer checks and dependency validation": ("checks", ()),
    "Install/repair SAM3 venv": ("ai-runtime-install", ("sam3",)),
    "Install/repair MatAnyone2 venv": ("ai-runtime-install", ("matanyone2",)),
    "Install/repair VideoMaMa venv": ("ai-runtime-install", ("videomama",)),
    "Install/repair SAM3 model": ("ai-install", ("sam3_transformers",)),
    "Install/repair MatAnyone2 model": ("ai-install", ("matanyone2",)),
    "Install/repair VideoMaMa model": ("ai-install", ("videomama",)),
}


def env_for_backend() -> dict[str, str]:
    env = os.environ.copy()
    env["FLUX_SETUP_INTERNAL_DISPATCH"] = "1"
    env.setdefault("PYTHONUNBUFFERED", "1")
    return env


class FluxInstallerGui:
    def __init__(self, root: Tk) -> None:
        self.root = root
        self.root.title("Flux Installer")
        self.root.geometry("1240x860")
        self.events: queue.Queue[tuple[str, str]] = queue.Queue()
        self.process: subprocess.Popen[str] | None = None
        self.buttons: list[ttk.Button] = []
        self.current_action = ""
        self.last_diagnostics: dict | None = None
        self.repair_vars: dict[str, BooleanVar] = {}
        self.cancel_button: ttk.Button | None = None

        self.style = ttk.Style()
        try:
            self.style.theme_use("clam")
        except Exception:
            pass
        self.style.configure("TFrame", background="#202124")
        self.style.configure("TLabel", background="#202124", foreground="#e8eaed")
        self.style.configure("TButton", padding=6)
        self.style.configure("TLabelframe", background="#202124", foreground="#e8eaed")
        self.style.configure("TLabelframe.Label", background="#202124", foreground="#e8eaed")
        self.style.configure("Treeview", background="#111315", foreground="#e8eaed", fieldbackground="#111315")
        self.style.configure("Treeview.Heading", background="#303134", foreground="#e8eaed")

        frame = ttk.Frame(root, padding=10)
        frame.pack(fill=BOTH, expand=True)
        ttk.Label(frame, text="Flux Installer", font=("Sans", 20, "bold")).pack(anchor=W)
        ttk.Label(frame, text="Install, repair, check, and troubleshoot the app, plugins/OFX, AI venvs, and model payloads.").pack(anchor=W, pady=(2, 10))

        self.notebook = ttk.Notebook(frame)
        self.notebook.pack(fill=BOTH, expand=True)
        self._status_page()
        self._repairs_page()
        self._app_page()
        self._plugins_page()
        self._ai_page()
        self._logs_page()

        self.progress = ttk.Progressbar(frame, mode="determinate", maximum=100)
        self.progress.pack(fill=X, pady=(10, 4))
        self.status = ttk.Label(frame, text="Ready")
        self.status.pack(anchor=W)

        self.root.after(100, self._pump_events)
        self.refresh_diagnostics(show_log=False)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _status_page(self) -> None:
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="Status")
        ttk.Label(tab, text="This is the install state. Red rows are the things to fix; use Suggested Repairs.").pack(anchor=W)
        columns = ("area", "state", "details")
        self.status_table = ttk.Treeview(tab, columns=columns, show="headings", height=18)
        for col, width in (("area", 260), ("state", 90), ("details", 760)):
            self.status_table.heading(col, text=col.title())
            self.status_table.column(col, width=width, anchor=W)
        self.status_table.tag_configure("ok", foreground="#8ab4f8")
        self.status_table.tag_configure("fail", foreground="#f28b82")
        self.status_table.tag_configure("warn", foreground="#fdd663")
        self.status_table.pack(fill=BOTH, expand=True, pady=8)
        row = ttk.Frame(tab)
        row.pack(fill=X)
        self._button(row, "Check installation now", lambda: self.run_action("checks", select_logs=True)).pack(side=LEFT, padx=(0, 6))
        self._button(row, "Refresh status", self.refresh_diagnostics).pack(side=LEFT, padx=6)

    def _repairs_page(self) -> None:
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="Suggested Repairs")
        ttk.Label(tab, text="Select the repairs you want. The installer shows the exact commands and live log output before and during repair.").pack(anchor=W)
        self.repair_frame = ttk.Frame(tab)
        self.repair_frame.pack(fill=BOTH, expand=True, pady=8)
        row = ttk.Frame(tab)
        row.pack(fill=X)
        self._button(row, "Run selected repairs", self.run_selected_repairs).pack(side=LEFT, padx=(0, 6))
        self._button(row, "Select all suggested", self.select_all_suggested).pack(side=LEFT, padx=6)
        self._button(row, "Clear selection", self.clear_repairs).pack(side=LEFT, padx=6)

    def _app_page(self) -> None:
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="App")
        ttk.Label(tab, text="App install/build actions. Output appears immediately in Logs.").pack(anchor=W, pady=(0, 8))
        for label, action in (
            ("Full bootstrap: deps → configure → build → deploy → checks", "full-bootstrap"),
            ("Installer self-test: fresh clone + stale submodule repair", "installer-self-test"),
            ("Install / repair app runtime, PyPlugs, OFX, launcher", "deploy-runtime"),
            ("Configure CMake", "configure"),
            ("Build Flux + OFX", "build-all"),
            ("Check install", "checks"),
            ("Launch Flux", "launch"),
        ):
            self._action_button(tab, label, action).pack(fill=X, pady=3)

    def _plugins_page(self) -> None:
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="Plugins / OFX")
        ttk.Label(tab, text="Repair and validate PyPlugs, Flux OFX bundles, OFX extras, OFX cache, and loader paths.").pack(anchor=W, pady=(0, 8))
        self._action_button(tab, "Repair plugins / OFX / launcher", "deploy-runtime").pack(fill=X, pady=3)
        self._action_button(tab, "Run plugin and dependency checks", "checks").pack(fill=X, pady=3)

    def _ai_page(self) -> None:
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="AI")
        ttk.Label(tab, text="Each model/provider has its own venv and self-check. Output appears in Logs.").pack(anchor=W)
        for title, runtime_id, model_id in (
            ("SAM3", "sam3", "sam3_transformers"),
            ("MatAnyone2", "matanyone2", "matanyone2"),
            ("VideoMaMa", "videomama", "videomama"),
            ("SAM3.1", "sam31", "sam31_sam3plus"),
        ):
            self._provider_box(tab, title, runtime_id, model_id).pack(fill=X, pady=8)

    def _logs_page(self) -> None:
        tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(tab, text="Logs")
        ttk.Label(tab, text="Live command output. This is where checks, failures, downloads, and repairs are shown.").pack(anchor=W)
        self.log = scrolledtext.ScrolledText(tab, bg="#111315", fg="#e8eaed", insertbackground="#e8eaed")
        self.log.pack(fill=BOTH, expand=True, pady=6)
        self.log.configure(state=DISABLED)
        row = ttk.Frame(tab)
        row.pack(fill=X)
        self._button(row, "Clear log", self.clear_log).pack(side=LEFT)
        self.cancel_button = self._button(row, "Cancel running action", self.cancel_action)
        self.cancel_button.pack(side=LEFT, padx=6)
        self.cancel_button.configure(state=DISABLED)

    def _provider_box(self, parent: ttk.Frame, title: str, runtime_id: str, model_id: str) -> ttk.LabelFrame:
        box = ttk.LabelFrame(parent, text=title, padding=8)
        ttk.Label(box, text=f"Runtime venv: {runtime_id}    Model payload: {model_id}").pack(anchor=W)
        row = ttk.Frame(box)
        row.pack(fill=X, pady=(6, 0))
        self._action_button(row, "Install / repair venv", "ai-runtime-install", runtime_id).pack(side=LEFT, padx=(0, 5))
        self._action_button(row, "Runtime status", "ai-runtime-status", runtime_id).pack(side=LEFT, padx=5)
        self._action_button(row, "Self-check", "ai-runtime-self-check", runtime_id).pack(side=LEFT, padx=5)
        self._action_button(row, "Download / repair model", "ai-install", model_id).pack(side=LEFT, padx=5)
        return box

    def _button(self, parent: ttk.Frame, text: str, command) -> ttk.Button:
        button = ttk.Button(parent, text=text, command=command)
        self.buttons.append(button)
        return button

    def _action_button(self, parent: ttk.Frame, text: str, action: str, *args: str) -> ttk.Button:
        return self._button(parent, text, lambda: self.run_action(action, *args, select_logs=True))

    def run_action(self, action: str, *args: str, select_logs: bool = True, auto_yes: bool = False) -> None:
        if self.process is not None:
            return
        if action == "launch":
            self.launch_flux_detached()
            return
        command_text = f"{SETUP_SCRIPT} __flux_setup_action {action} {' '.join(args)}".strip()
        if action in MUTATING_ACTIONS and not auto_yes:
            if not messagebox.askyesno("Confirm repair/install action", f"Run this action?\n\n{command_text}"):
                return
            auto_yes = True
        if select_logs:
            self.notebook.select(self.notebook.tabs()[-1])
        self._set_buttons(DISABLED)
        if self.cancel_button is not None:
            self.cancel_button.configure(state=NORMAL)
        self.progress.configure(mode="indeterminate")
        self.progress.start(12)
        self.current_action = f"{action} {' '.join(args)}".strip()
        self.status.configure(text=f"Running {self.current_action}")
        self._append_log(f"\n=== Running: {command_text} ===\n")
        thread = threading.Thread(target=self._run_process, args=(action, args, auto_yes), daemon=True)
        thread.start()

    def launch_flux_detached(self) -> None:
        cmd = [str(SETUP_SCRIPT), "__flux_setup_action", "launch"]
        try:
            subprocess.Popen(
                cmd,
                cwd=str(FLUX_ROOT),
                env=env_for_backend(),
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            self.status.configure(text="Flux launched")
        except Exception as exc:
            self._append_log(f"ERROR: failed to launch Flux: {exc}\n")
            self.status.configure(text="Launch failed")

    def cancel_action(self) -> None:
        process = self.process
        if process is None:
            return
        self._append_log("=== Cancelling running action ===\n")
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        except Exception:
            process.terminate()


    def _run_process(self, action: str, args: tuple[str, ...], auto_yes: bool) -> None:
        cmd = [str(SETUP_SCRIPT), "__flux_setup_action", action, *args]
        try:
            self.process = subprocess.Popen(
                cmd,
                cwd=str(FLUX_ROOT),
                env=env_for_backend(),
                text=True,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                bufsize=1,
                start_new_session=True,
            )
            if auto_yes and self.process.stdin is not None:
                try:
                    self.process.stdin.write("y\n" * 8)
                    self.process.stdin.flush()
                except BrokenPipeError:
                    pass
            if self.process.stdin is not None:
                self.process.stdin.close()
            assert self.process.stdout is not None
            for line in self.process.stdout:
                self.events.put(("log", line))
            code = self.process.wait()
            self.events.put(("done", str(code)))
            self.process = None
        except Exception as exc:
            self.events.put(("error", str(exc)))
        finally:
            self.process = None

    def refresh_diagnostics(self, show_log: bool = True) -> None:
        if show_log:
            self._append_log("\n=== Refreshing installation status ===\n")
        try:
            completed = subprocess.run(
                [str(SETUP_SCRIPT), "__flux_setup_action", "diagnostics-json"],
                cwd=str(FLUX_ROOT),
                env=env_for_backend(),
                text=True,
                capture_output=True,
                timeout=20,
                check=False,
            )
            raw = completed.stdout or completed.stderr
            data = json.loads(raw)
            self.last_diagnostics = data
            self._render_status(data)
            self._render_repairs(data)
            if show_log:
                self._append_log(self._human_report(data) + "\n")
        except Exception as exc:
            self.last_diagnostics = None
            self._replace_table_rows([("Diagnostics", "FAIL", str(exc), "fail")])
            self._append_log(f"Diagnostics failed: {exc}\n")

    def _render_status(self, data: dict) -> None:
        rows: list[tuple[str, str, str, str]] = []
        app = data.get("app", {})
        for name, label in (("build_binary", "Build binary"), ("installed_app", "Installed app"), ("launcher", "Launcher")):
            item = app.get(name, {})
            ok = bool(item.get("exists")) and (name != "installed_app" or bool(item.get("executable")))
            rows.append((label, "OK" if ok else "FAIL", item.get("path", "missing"), "ok" if ok else "fail"))
        plugins = data.get("plugins", {})
        for item in plugins.get("required_pyplugs", []):
            ok = bool(item.get("installed"))
            rows.append(("PyPlug", "OK" if ok else "FAIL", item.get("name", ""), "ok" if ok else "fail"))
        for item in plugins.get("required_ofx", []):
            ok = bool(item.get("installed"))
            rows.append(("OFX bundle", "OK" if ok else "FAIL", item.get("name", ""), "ok" if ok else "fail"))
        ai = data.get("ai", {})
        for item in ai.get("runtimes", []):
            ok = bool(item.get("installed"))
            rows.append((f"AI runtime {item.get('id')}", "OK" if ok else "FAIL", item.get("venv_python", ""), "ok" if ok else "fail"))
        for item in ai.get("models", []):
            ok = bool(item.get("present"))
            rows.append((f"AI model {item.get('id')}", "OK" if ok else "FAIL", item.get("path", ""), "ok" if ok else "fail"))
        self._replace_table_rows(rows)

    def _replace_table_rows(self, rows: list[tuple[str, str, str, str]]) -> None:
        for row in self.status_table.get_children():
            self.status_table.delete(row)
        for area, state, details, tag in rows:
            self.status_table.insert("", END, values=(area, state, details), tags=(tag,))

    def _render_repairs(self, data: dict) -> None:
        for child in self.repair_frame.winfo_children():
            child.destroy()
        self.repair_vars.clear()
        suggested = self._suggest_repairs(data)
        if not suggested:
            ttk.Label(self.repair_frame, text="No repair suggestions from the current status. Run checks if something still fails.").pack(anchor=W)
            return
        for label in suggested:
            var = BooleanVar(value=True)
            self.repair_vars[label] = var
            ttk.Checkbutton(self.repair_frame, text=label, variable=var).pack(anchor=W, pady=3)

    def _suggest_repairs(self, data: dict) -> list[str]:
        suggestions: list[str] = []
        app = data.get("app", {})
        if not app.get("build_binary", {}).get("exists"):
            suggestions.append("Build Flux and bundled OFX plugins")
        if not app.get("installed_app", {}).get("exists") or not app.get("launcher", {}).get("exists"):
            suggestions.append("Install/repair the app, bundled Python tools, PyPlugs, OFX bundles, launcher, and OFX cache")
        plugins = data.get("plugins", {})
        if any(not x.get("installed") for x in plugins.get("required_pyplugs", [])) or any(not x.get("installed") for x in plugins.get("required_ofx", [])):
            suggestions.append("Install/repair the app, bundled Python tools, PyPlugs, OFX bundles, launcher, and OFX cache")
        ai = data.get("ai", {})
        runtime_map = {"sam3": "Install/repair SAM3 venv", "matanyone2": "Install/repair MatAnyone2 venv", "videomama": "Install/repair VideoMaMa venv"}
        for item in ai.get("runtimes", []):
            label = runtime_map.get(item.get("id"))
            if label and not item.get("installed"):
                suggestions.append(label)
        model_map = {"sam3_transformers": "Install/repair SAM3 model", "matanyone2": "Install/repair MatAnyone2 model", "videomama": "Install/repair VideoMaMa model"}
        for item in ai.get("models", []):
            label = model_map.get(item.get("id"))
            if label and not item.get("present"):
                suggestions.append(label)
        suggestions.append("Run installer checks and dependency validation")
        deduped: list[str] = []
        for s in suggestions:
            if s not in deduped:
                deduped.append(s)
        return deduped

    def run_selected_repairs(self) -> None:
        selected = [label for label, var in self.repair_vars.items() if var.get()]
        if not selected:
            messagebox.showinfo("No repairs selected", "Select at least one repair action.")
            return
        details = "\n".join(f"• {label}" for label in selected)
        if not messagebox.askyesno("Run selected repairs", f"Run these repair actions?\n\n{details}"):
            return
        self.notebook.select(self.notebook.tabs()[-1])
        self._append_log("\n=== Selected repairs ===\n" + details + "\n")
        self._repair_queue = selected
        self._run_next_repair()

    def _run_next_repair(self) -> None:
        if not getattr(self, "_repair_queue", None):
            self.refresh_diagnostics()
            return
        label = self._repair_queue.pop(0)
        action, args = REPAIR_ACTIONS[label]
        self.run_action(action, *args, select_logs=True, auto_yes=True)

    def select_all_suggested(self) -> None:
        for var in self.repair_vars.values():
            var.set(True)

    def clear_repairs(self) -> None:
        for var in self.repair_vars.values():
            var.set(False)

    def _human_report(self, data: dict) -> str:
        lines = ["Installation status:"]
        for row in self.status_table.get_children():
            area, state, details = self.status_table.item(row, "values")
            lines.append(f"  {state:4} {area}: {details}")
        return "\n".join(lines)

    def _pump_events(self) -> None:
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "log":
                self._append_log(payload)
            elif kind == "done":
                self.progress.stop()
                ok = payload == "0"
                self.progress.configure(mode="determinate", value=100 if ok else 0)
                self.status.configure(text=f"{self.current_action}: {'completed' if ok else 'failed with exit code ' + payload}")
                self._append_log(f"=== {self.current_action} {'completed' if ok else 'failed'} ===\n")
                self._set_buttons(NORMAL)
                if self.cancel_button is not None:
                    self.cancel_button.configure(state=DISABLED)
                if getattr(self, "_repair_queue", None) and ok:
                    self.root.after(150, self._run_next_repair)
                else:
                    self.refresh_diagnostics(show_log=False)
            elif kind == "error":
                self.progress.stop()
                self.status.configure(text="Failed")
                self._append_log(payload + "\n")
                self._set_buttons(NORMAL)
                if self.cancel_button is not None:
                    self.cancel_button.configure(state=DISABLED)
        self.root.after(100, self._pump_events)

    def _append_log(self, text: str) -> None:
        self.log.configure(state=NORMAL)
        self.log.insert(END, text)
        self.log.see(END)
        self.log.configure(state=DISABLED)

    def clear_log(self) -> None:
        self.log.configure(state=NORMAL)
        self.log.delete("1.0", END)
        self.log.configure(state=DISABLED)

    def _set_buttons(self, state: str) -> None:
        for button in self.buttons:
            button.configure(state=state)


    def close(self) -> None:
        self.cancel_action()
        self.root.destroy()

def main() -> int:
    try:
        root = Tk()
    except Exception as exc:
        print(f"ERROR: cannot start graphical installer: {exc}", file=sys.stderr)
        print("Run tools/linux/flux-linux-setup.sh --tui for the terminal fallback.", file=sys.stderr)
        return 2
    FluxInstallerGui(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
