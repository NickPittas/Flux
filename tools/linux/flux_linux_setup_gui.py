#!/usr/bin/env python3
"""Flux graphical installer/repair/troubleshooter.

Run directly from a fresh checkout with:
    tools/linux/flux-linux-setup.sh
"""
from __future__ import annotations

import json
import os
import signal
import subprocess
import shutil
import sys
from pathlib import Path
from typing import Any

try:
    from PySide6.QtCore import QProcess, QTimer, Qt, QSize
    from PySide6.QtGui import QClipboard, QFont, QIcon, QPalette, QPixmap, QColor, QTextCursor
    from PySide6.QtWidgets import (
        QApplication,
        QCheckBox,
        QAbstractItemView,
        QFileDialog,
        QFrame,
        QGridLayout,
        QGroupBox,
        QHBoxLayout,
        QLabel,
        QLineEdit,
        QMainWindow,
        QMessageBox,
        QPlainTextEdit,
        QProgressBar,
        QPushButton,
        QScrollArea,
        QSizePolicy,
        QTabWidget,
        QTableWidget,
        QTableWidgetItem,
        QVBoxLayout,
        QWidget,
    )
except Exception as exc:  # pragma: no cover - exercised by shell fallback.
    print(f"ERROR: cannot import PySide6 for the Flux graphical installer: {exc}", file=sys.stderr)
    print("Install python3-pyside6 or run tools/linux/flux-linux-setup.sh --tui.", file=sys.stderr)
    raise SystemExit(2)

SCRIPT_DIR = Path(__file__).resolve().parent
SETUP_SCRIPT = SCRIPT_DIR / "flux-linux-setup.sh"
FLUX_ROOT = SCRIPT_DIR.parent.parent
SPLASH_IMAGE = FLUX_ROOT / "Gui" / "Resources" / "Images" / "splashscreen.png"
APP_ICON = FLUX_ROOT / "Gui" / "Resources" / "Images" / "natronIcon256_linux.png"

COLORS = {
    "accent": "#4285F4",
    "base": "#202026",
    "raised": "#303038",
    "sunken": "#16161A",
    "text": "#D2D2D7",
    "muted": "#92929D",
    "success": "#4CAF50",
    "warning": "#FFAB00",
    "info": "#78B4FF",
    "danger": "#F06464",
    "hover": "#FFBF78",
}

MUTATING_ACTIONS = {
    "full-bootstrap",
    "update-installed",
    "deploy-runtime",
    "fedora-deps",
    "rpmfusion",
    "uninstall",
    "ai-install",
    "ai-remove",
    "ai-runtime-install",
}

REPAIR_ACTIONS: dict[str, tuple[str, tuple[str, ...]]] = {
    "Install/repair the app, bundled Python tools, PyPlugs, OFX bundles, launcher, and OFX cache": ("deploy-runtime", ()),
    "Build Flux and bundled OFX plugins": ("build-all", ()),
    "Run installer checks and dependency validation": ("checks", ()),
    "Install/repair SAM3 venv": ("ai-runtime-install", ("sam3",)),
    "Install/repair MatAnyone2 venv": ("ai-runtime-install", ("matanyone2",)),
    "Install/repair VideoMaMa venv": ("ai-runtime-install", ("videomama",)),
    "Install/repair SAM3.1 venv": ("ai-runtime-install", ("sam31",)),
    "Install/repair SAM3 model": ("ai-install", ("sam3_transformers",)),
    "Install/repair MatAnyone2 model": ("ai-install", ("matanyone2",)),
    "Install/repair VideoMaMa model": ("ai-install", ("videomama",)),
    "Install/repair SAM3.1 model": ("ai-install", ("sam31_sam3plus",)),
}

AI_PROVIDERS = (
    ("SAM3", "sam3", "sam3_transformers"),
    ("MatAnyone2", "matanyone2", "matanyone2"),
    ("VideoMaMa", "videomama", "videomama"),
    ("SAM3.1", "sam31", "sam31_sam3plus"),
)


def env_for_backend() -> dict[str, str]:
    env = os.environ.copy()
    env["FLUX_SETUP_INTERNAL_DISPATCH"] = "1"
    env["PYTHONUNBUFFERED"] = "1"
    return env


def state_text(ok: bool) -> str:
    return "OK" if ok else "Needs attention"


class StatusCard(QFrame):
    def __init__(self, title: str, subtitle: str = "Unknown") -> None:
        super().__init__()
        self.setObjectName("StatusCard")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 12, 14, 12)
        layout.setSpacing(5)
        self.title = QLabel(title)
        self.title.setObjectName("CardTitle")
        self.value = QLabel(subtitle)
        self.value.setObjectName("CardValue")
        self.value.setWordWrap(True)
        layout.addWidget(self.title)
        layout.addWidget(self.value)

    def set_state(self, text: str, color: str) -> None:
        self.value.setText(text)
        self.value.setStyleSheet(f"color: {color};")


class FluxInstallerWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Flux Installer")
        self.setMinimumSize(1180, 780)
        if APP_ICON.is_file():
            self.setWindowIcon(QIcon(str(APP_ICON)))

        self.process: QProcess | None = None
        self.current_action = ""
        self.repair_queue: list[str] = []
        self.repair_checks: dict[str, QCheckBox] = {}
        self.buttons: list[QPushButton] = []
        self.last_diagnostics: dict[str, Any] | None = None

        self._build_ui()
        self._apply_theme()
        QTimer.singleShot(0, lambda: self.refresh_diagnostics(show_log=False))

    def _build_ui(self) -> None:
        root = QWidget()
        self.setCentralWidget(root)
        shell = QHBoxLayout(root)
        shell.setContentsMargins(0, 0, 0, 0)
        shell.setSpacing(0)

        rail = QFrame()
        rail.setObjectName("LeftRail")
        rail.setFixedWidth(330)
        rail_layout = QVBoxLayout(rail)
        rail_layout.setContentsMargins(18, 18, 18, 18)
        rail_layout.setSpacing(14)

        splash = QLabel()
        splash.setObjectName("Splash")
        splash.setAlignment(Qt.AlignCenter)
        splash.setMinimumHeight(140)
        if SPLASH_IMAGE.is_file():
            pix = QPixmap(str(SPLASH_IMAGE))
            splash.setPixmap(pix.scaled(QSize(292, 150), Qt.KeepAspectRatio, Qt.SmoothTransformation))
        else:
            splash.setText("Flux")
        rail_layout.addWidget(splash)

        title = QLabel("Flux Installer")
        title.setObjectName("HeroTitle")
        subtitle = QLabel("Install, repair, check, and troubleshoot Flux, OpenFX plugins, and AI model runtimes.")
        subtitle.setObjectName("HeroSubtitle")
        subtitle.setWordWrap(True)
        rail_layout.addWidget(title)
        rail_layout.addWidget(subtitle)

        self.rail_cards: dict[str, StatusCard] = {}
        for key, label in (("app", "App"), ("plugins", "Plugins"), ("ai", "AI"), ("gpu", "GPU / OpenGL"), ("deps", "Fedora deps")):
            card = StatusCard(label)
            self.rail_cards[key] = card
            rail_layout.addWidget(card)

        rail_layout.addSpacing(4)
        self._rail_button(rail_layout, "Full Bootstrap", "full-bootstrap")
        self._rail_button(rail_layout, "Update Installed Flux", "update-installed")
        self._rail_button(rail_layout, "Launch Flux", "launch")
        rail_layout.addStretch(1)

        workspace = QFrame()
        workspace.setObjectName("Workspace")
        work_layout = QVBoxLayout(workspace)
        work_layout.setContentsMargins(18, 18, 18, 12)
        work_layout.setSpacing(10)

        self.tabs = QTabWidget()
        self.tabs.setDocumentMode(True)
        work_layout.addWidget(self.tabs, 1)
        self._overview_tab()
        self._install_tab()
        self._repairs_tab()
        self._plugins_tab()
        self._ai_tab()
        self._logs_tab()

        bottom = QHBoxLayout()
        self.progress = QProgressBar()
        self.progress.setTextVisible(False)
        self.progress.setMaximum(100)
        self.status = QLabel("Ready")
        self.status.setObjectName("FooterStatus")
        bottom.addWidget(self.progress, 1)
        bottom.addWidget(self.status, 0)
        work_layout.addLayout(bottom)

        shell.addWidget(rail)
        shell.addWidget(workspace, 1)

    def _overview_tab(self) -> None:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(14, 14, 14, 14)
        intro = QLabel("Overview")
        intro.setObjectName("PageTitle")
        layout.addWidget(intro)
        grid = QGridLayout()
        grid.setSpacing(10)
        self.overview_cards: dict[str, StatusCard] = {}
        for idx, key_label in enumerate((("build", "Build binary"), ("installed", "Installed app"), ("launcher", "Launcher"), ("pyplugs", "PyPlugs"), ("ofx", "OFX bundles"), ("ai_tools", "AI tools"), ("models", "AI models"), ("runtimes", "AI runtimes"))):
            key, label = key_label
            card = StatusCard(label)
            self.overview_cards[key] = card
            grid.addWidget(card, idx // 4, idx % 4)
        layout.addLayout(grid)
        self.recommendation = QLabel("Refreshing installer diagnostics…")
        self.recommendation.setObjectName("Recommendation")
        self.recommendation.setWordWrap(True)
        layout.addWidget(self.recommendation)
        self.status_table = QTableWidget(0, 3)
        self.status_table.setHorizontalHeaderLabels(["Area", "State", "Details"])
        self.status_table.horizontalHeader().setStretchLastSection(True)
        self.status_table.verticalHeader().setVisible(False)
        self.status_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.status_table.setSelectionBehavior(QAbstractItemView.SelectRows)
        layout.addWidget(self.status_table, 1)
        row = QHBoxLayout()
        row.addWidget(self._button("Refresh Status", lambda: self.refresh_diagnostics(show_log=True)))
        row.addWidget(self._button("Run Checks", lambda: self.run_action("checks", select_logs=True)))
        row.addStretch(1)
        layout.addLayout(row)
        self.tabs.addTab(tab, "Overview")

    def _install_tab(self) -> None:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.addWidget(self._section_label("Install / Update", "Bootstrap, update, build, deploy, and validate Flux."))
        actions = (
            ("Full bootstrap", "Deps → configure → build → deploy → checks", "full-bootstrap", ()),
            ("Update installed Flux", "Update checkout, configure, build, deploy, and clear scoped OFX cache", "update-installed", ()),
            ("Deploy runtime", "Install app runtime, PyPlugs, OFX bundles, launcher, and OFX cache", "deploy-runtime", ()),
            ("Configure CMake", "Configure the Flux build tree", "configure", ()),
            ("Build Flux + OFX", "Build the app and bundled OpenFX plugins", "build-all", ()),
            ("Install Fedora dependencies", "Install Fedora packages required by the build", "fedora-deps", ()),
            ("Enable RPM Fusion free", "Enable RPM Fusion repository required by media dependencies", "rpmfusion", ()),
            ("Installer self-test", "Validate fresh clone and stale submodule repair paths", "installer-self-test", ()),
            ("Check install", "Run installer checks and dependency validation", "checks", ()),
            ("Uninstall Flux runtime", "Remove installed app runtime and launcher", "uninstall", ()),
        )
        for title, detail, action, args in actions:
            layout.addWidget(self._action_row(title, detail, action, *args))
        layout.addStretch(1)
        self.tabs.addTab(tab, "Install / Update")

    def _repairs_tab(self) -> None:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.addWidget(self._section_label("Suggested Repairs", "Generated from diagnostics failures. Select the repairs to run."))
        self.repair_container = QVBoxLayout()
        scroll_body = QWidget()
        scroll_body.setLayout(self.repair_container)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(scroll_body)
        layout.addWidget(scroll, 1)
        row = QHBoxLayout()
        row.addWidget(self._button("Run Selected Repairs", self.run_selected_repairs))
        row.addWidget(self._button("Select All", self.select_all_suggested))
        row.addWidget(self._button("Clear", self.clear_repairs))
        row.addStretch(1)
        layout.addLayout(row)
        self.tabs.addTab(tab, "Repairs")

    def _plugins_tab(self) -> None:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.addWidget(self._section_label("Plugins / OFX", "Repair and validate PyPlugs, Flux OFX bundles, OFX extras, cache, and loader paths."))
        self.plugins_summary = QLabel("Refresh diagnostics to inspect plugin state.")
        self.plugins_summary.setObjectName("MonoBlock")
        self.plugins_summary.setWordWrap(True)
        layout.addWidget(self.plugins_summary)
        layout.addWidget(self._action_row("Repair plugins / OFX / launcher", "Deploy all runtime plugin payloads and clear scoped OFX cache", "deploy-runtime"))
        layout.addWidget(self._action_row("Run plugin and dependency checks", "Validate plugin payloads and dynamic library dependencies", "checks"))
        layout.addStretch(1)
        self.tabs.addTab(tab, "Plugins / OFX")

    def _ai_tab(self) -> None:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.addWidget(self._section_label("AI Models", "Runtime environments and model payloads for AI matte/depth providers."))
        token_row = QHBoxLayout()
        token_label = QLabel("Hugging Face token")
        self.token_edit = QLineEdit()
        self.token_edit.setEchoMode(QLineEdit.Password)
        self.token_edit.setPlaceholderText("Optional; backend token action remains authoritative")
        token_row.addWidget(token_label)
        token_row.addWidget(self.token_edit, 1)
        token_row.addWidget(self._button("Open Token Entry", lambda: self.run_action("ai-token", select_logs=True)))
        layout.addLayout(token_row)
        for title, runtime_id, model_id in AI_PROVIDERS:
            group = QGroupBox(title)
            group.setObjectName("ProviderBox")
            box = QVBoxLayout(group)
            box.addWidget(QLabel(f"Runtime venv: {runtime_id}    Model payload: {model_id}"))
            row = QHBoxLayout()
            row.addWidget(self._small_action("Install / repair venv", "ai-runtime-install", runtime_id))
            row.addWidget(self._small_action("Runtime status", "ai-runtime-status", runtime_id))
            row.addWidget(self._small_action("Self-check", "ai-runtime-self-check", runtime_id))
            row.addWidget(self._small_action("Model status", "ai-status", model_id))
            row.addWidget(self._small_action("Download / repair model", "ai-install", model_id))
            row.addStretch(1)
            box.addLayout(row)
            layout.addWidget(group)
        layout.addStretch(1)
        self.tabs.addTab(tab, "AI Models")

    def _logs_tab(self) -> None:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.addWidget(self._section_label("Logs", "Live backend output. Failures are never hidden."))
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(20000)
        self.log.setObjectName("LogView")
        layout.addWidget(self.log, 1)
        row = QHBoxLayout()
        row.addWidget(self._button("Clear", self.clear_log))
        row.addWidget(self._button("Copy", self.copy_log))
        row.addWidget(self._button("Save Log…", self.save_log))
        self.cancel_button = self._button("Cancel Running Action", self.cancel_action)
        self.cancel_button.setEnabled(False)
        row.addWidget(self.cancel_button)
        row.addStretch(1)
        layout.addLayout(row)
        self.tabs.addTab(tab, "Logs")

    def _rail_button(self, layout: QVBoxLayout, text: str, action: str) -> None:
        button = self._button(text, lambda a=action: self.run_action(a, select_logs=True))
        button.setObjectName("PrimaryButton" if action != "launch" else "LaunchButton")
        layout.addWidget(button)

    def _button(self, text: str, callback) -> QPushButton:
        button = QPushButton(text)
        button.clicked.connect(callback)
        button.setCursor(Qt.PointingHandCursor)
        self.buttons.append(button)
        return button

    def _small_action(self, text: str, action: str, *args: str) -> QPushButton:
        button = self._button(text, lambda: self.run_action(action, *args, select_logs=True))
        button.setSizePolicy(QSizePolicy.Maximum, QSizePolicy.Fixed)
        return button

    def _action_row(self, title: str, detail: str, action: str, *args: str) -> QFrame:
        frame = QFrame()
        frame.setObjectName("ActionRow")
        layout = QHBoxLayout(frame)
        text = QVBoxLayout()
        label = QLabel(title)
        label.setObjectName("ActionTitle")
        desc = QLabel(detail)
        desc.setObjectName("ActionDetail")
        desc.setWordWrap(True)
        text.addWidget(label)
        text.addWidget(desc)
        layout.addLayout(text, 1)
        layout.addWidget(self._small_action("Run", action, *args))
        return frame

    def _section_label(self, title: str, detail: str) -> QWidget:
        box = QWidget()
        layout = QVBoxLayout(box)
        layout.setContentsMargins(0, 0, 0, 8)
        label = QLabel(title)
        label.setObjectName("PageTitle")
        desc = QLabel(detail)
        desc.setObjectName("PageDetail")
        desc.setWordWrap(True)
        layout.addWidget(label)
        layout.addWidget(desc)
        return box

    def run_action(self, action: str, *args: str, select_logs: bool = True, auto_yes: bool = False) -> None:
        if self.process is not None:
            self.append_log("\n=== Refusing to start new action; another action is running ===\n")
            return
        if action == "launch":
            self.launch_flux_detached()
            return
        command_text = f"{SETUP_SCRIPT} __flux_setup_action {action} {' '.join(args)}".strip()
        if action in MUTATING_ACTIONS and not auto_yes:
            if QMessageBox.question(self, "Confirm repair/install action", f"Run this action?\n\n{command_text}") != QMessageBox.Yes:
                return
            auto_yes = True
        if select_logs:
            self.tabs.setCurrentIndex(self.tabs.count() - 1)
        self.set_buttons_enabled(False)
        self.cancel_button.setEnabled(True)
        self.progress.setRange(0, 0)
        self.current_action = f"{action} {' '.join(args)}".strip()
        self.status.setText(f"Running {self.current_action}")
        self.append_log(f"\n=== Running: {command_text} ===\n")

        process = QProcess(self)
        setsid = shutil.which("setsid")
        if setsid:
            process.setProgram(setsid)
            process.setArguments([str(SETUP_SCRIPT), "__flux_setup_action", action, *args])
        else:
            process.setProgram(str(SETUP_SCRIPT))
            process.setArguments(["__flux_setup_action", action, *args])
        process.setWorkingDirectory(str(FLUX_ROOT))
        env = process.processEnvironment()
        for key, value in env_for_backend().items():
            env.insert(key, value)
        process.setProcessEnvironment(env)
        process.setProcessChannelMode(QProcess.MergedChannels)
        process.readyReadStandardOutput.connect(self._read_process_output)
        process.finished.connect(self._process_finished)
        process.errorOccurred.connect(self._process_error)
        self.process = process
        process.start()
        if not process.waitForStarted(5000):
            self.append_log(f"ERROR: failed to start {command_text}\n")
            self._finish_action(False, "failed to start")
            return
        if auto_yes:
            process.write(("y\n" * 8).encode("utf-8"))
            process.closeWriteChannel()

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
            self.status.setText("Flux launched")
            self.append_log("\n=== Launch Flux dispatched detached ===\n")
        except Exception as exc:
            self.append_log(f"ERROR: failed to launch Flux: {exc}\n")
            self.status.setText("Launch failed")

    def cancel_action(self) -> None:
        process = self.process
        if process is None:
            return
        self.append_log("=== Cancelling running action ===\n")
        pid = int(process.processId())
        if pid > 0:
            try:
                os.killpg(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            except Exception:
                process.terminate()
        else:
            process.terminate()
        QTimer.singleShot(2500, self._kill_if_running)

    def _kill_if_running(self) -> None:
        process = self.process
        if process is not None and process.state() != QProcess.NotRunning:
            pid = int(process.processId())
            if pid > 0:
                try:
                    os.killpg(pid, signal.SIGKILL)
                except Exception:
                    process.kill()
            else:
                process.kill()

    def _read_process_output(self) -> None:
        if self.process is None:
            return
        data = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        self.append_log(data)

    def _process_error(self, error) -> None:
        self.append_log(f"ERROR: process error: {error}\n")

    def _process_finished(self, code: int, _status) -> None:
        ok = code == 0
        self.append_log(f"=== {self.current_action} {'completed' if ok else f'failed with exit code {code}'} ===\n")
        queued = bool(self.repair_queue)
        self._finish_action(ok, "completed" if ok else f"failed with exit code {code}")
        if queued and ok:
            QTimer.singleShot(150, self._run_next_repair)
        else:
            self.repair_queue.clear()
            self.refresh_diagnostics(show_log=False)

    def _finish_action(self, ok: bool, message: str) -> None:
        self.progress.setRange(0, 100)
        self.progress.setValue(100 if ok else 0)
        self.status.setText(f"{self.current_action}: {message}" if self.current_action else message)
        self.set_buttons_enabled(True)
        self.cancel_button.setEnabled(False)
        if self.process is not None:
            self.process.deleteLater()
        self.process = None

    def refresh_diagnostics(self, show_log: bool = True) -> None:
        if self.process is not None:
            return
        if show_log:
            self.append_log("\n=== Refreshing installation status ===\n")
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
        try:
            data = json.loads(raw)
        except Exception as exc:
            self.last_diagnostics = None
            self._replace_status_rows([("Diagnostics", "FAIL", f"{exc}: {raw[:500]}", COLORS["danger"])])
            self.append_log(f"Diagnostics failed: {exc}\n{raw}\n")
            return
        self.last_diagnostics = data
        self._render_status(data)
        self._render_repairs(data)
        if show_log:
            self.append_log(self._human_report() + "\n")

    def _render_status(self, data: dict[str, Any]) -> None:
        rows: list[tuple[str, str, str, str]] = []
        app = data.get("app", {})
        build_ok = bool(app.get("build_binary", {}).get("exists"))
        installed = app.get("installed_app", {})
        installed_ok = bool(installed.get("exists")) and bool(installed.get("executable"))
        launcher_ok = bool(app.get("launcher", {}).get("exists")) and bool(app.get("launcher", {}).get("executable"))
        for name, label, ok in (("build_binary", "Build binary", build_ok), ("installed_app", "Installed app", installed_ok), ("launcher", "Launcher", launcher_ok)):
            item = app.get(name, {})
            rows.append((label, "OK" if ok else "FAIL", item.get("path", "missing"), COLORS["success"] if ok else COLORS["danger"]))

        plugins = data.get("plugins", {})
        pyplugs = plugins.get("required_pyplugs", [])
        ofx = plugins.get("required_ofx", [])
        for item in pyplugs:
            ok = bool(item.get("installed"))
            rows.append(("PyPlug", "OK" if ok else "FAIL", item.get("name", ""), COLORS["success"] if ok else COLORS["danger"]))
        for item in ofx:
            ok = bool(item.get("installed"))
            rows.append(("OFX bundle", "OK" if ok else "FAIL", item.get("name", ""), COLORS["success"] if ok else COLORS["danger"]))

        ai = data.get("ai", {})
        runtimes = ai.get("runtimes", [])
        models = ai.get("models", [])
        for item in runtimes:
            ok = bool(item.get("installed"))
            rows.append((f"AI runtime {item.get('id')}", "OK" if ok else "FAIL", item.get("venv_python", ""), COLORS["success"] if ok else COLORS["warning"]))
        for item in models:
            ok = bool(item.get("present"))
            rows.append((f"AI model {item.get('id')}", "OK" if ok else "FAIL", item.get("path", ""), COLORS["success"] if ok else COLORS["warning"]))
        self._replace_status_rows(rows)

        pyplug_ok = all(x.get("installed") for x in pyplugs)
        ofx_ok = all(x.get("installed") for x in ofx)
        runtime_ok = all(x.get("installed") for x in runtimes)
        model_ok = all(x.get("present") for x in models)
        ai_tools = ai.get("tools", {})
        ai_tools_ok = all(bool(v) for v in ai_tools.values()) if ai_tools else False
        self._set_card(self.rail_cards["app"], installed_ok and launcher_ok, "Installed and launchable" if installed_ok and launcher_ok else "Install or deploy needed")
        self._set_card(self.rail_cards["plugins"], pyplug_ok and ofx_ok, "PyPlugs and OFX present" if pyplug_ok and ofx_ok else "Plugin repair suggested")
        self._set_card(self.rail_cards["ai"], ai_tools_ok and runtime_ok and model_ok, "Runtimes and models present" if ai_tools_ok and runtime_ok and model_ok else "Optional AI setup incomplete")
        self.rail_cards["gpu"].set_state("Checked by installer logs", COLORS["info"])
        self.rail_cards["deps"].set_state("Run checks for package audit", COLORS["info"])
        self._set_card(self.overview_cards["build"], build_ok, state_text(build_ok))
        self._set_card(self.overview_cards["installed"], installed_ok, state_text(installed_ok))
        self._set_card(self.overview_cards["launcher"], launcher_ok, state_text(launcher_ok))
        self._set_card(self.overview_cards["pyplugs"], pyplug_ok, f"{sum(1 for x in pyplugs if x.get('installed'))}/{len(pyplugs)} installed")
        self._set_card(self.overview_cards["ofx"], ofx_ok, f"{sum(1 for x in ofx if x.get('installed'))}/{len(ofx)} installed")
        self._set_card(self.overview_cards["ai_tools"], ai_tools_ok, state_text(ai_tools_ok))
        self._set_card(self.overview_cards["models"], model_ok, f"{sum(1 for x in models if x.get('present'))}/{len(models)} present")
        self._set_card(self.overview_cards["runtimes"], runtime_ok, f"{sum(1 for x in runtimes if x.get('installed'))}/{len(runtimes)} installed")
        suggestions = self._suggest_repairs(data)
        if suggestions:
            self.recommendation.setText("Recommended next action: " + suggestions[0])
            self.recommendation.setStyleSheet(f"color: {COLORS['warning']};")
        else:
            self.recommendation.setText("No repair suggestions from current diagnostics.")
            self.recommendation.setStyleSheet(f"color: {COLORS['success']};")
        self._render_plugins_summary(data)

    def _set_card(self, card: StatusCard, ok: bool, text: str) -> None:
        card.set_state(text, COLORS["success"] if ok else COLORS["warning"])

    def _replace_status_rows(self, rows: list[tuple[str, str, str, str]]) -> None:
        self.status_table.setRowCount(len(rows))
        for row, (area, state, details, color) in enumerate(rows):
            for col, value in enumerate((area, state, details)):
                item = QTableWidgetItem(value)
                item.setForeground(QColor(color if col == 1 else COLORS["text"]))
                self.status_table.setItem(row, col, item)
        self.status_table.resizeColumnsToContents()

    def _render_plugins_summary(self, data: dict[str, Any]) -> None:
        plugins = data.get("plugins", {})
        paths = data.get("paths", {})
        lines = [
            f"PyPlug dir: {plugins.get('pyplug_dir', {}).get('path', 'unknown')}",
            f"OFX dir: {plugins.get('ofx_dir', {}).get('path', 'unknown')}",
            f"OFX cache: {paths.get('ofx_cache', 'unknown')}",
            "",
            "Required PyPlugs:",
        ]
        lines.extend(f"  {'OK' if x.get('installed') else 'FAIL'}  {x.get('name')}" for x in plugins.get("required_pyplugs", []))
        lines.append("Required OFX bundles:")
        lines.extend(f"  {'OK' if x.get('installed') else 'FAIL'}  {x.get('name')}" for x in plugins.get("required_ofx", []))
        self.plugins_summary.setText("\n".join(lines))

    def _render_repairs(self, data: dict[str, Any]) -> None:
        while self.repair_container.count():
            item = self.repair_container.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()
        self.repair_checks.clear()
        suggestions = self._suggest_repairs(data)
        if not suggestions:
            self.repair_container.addWidget(QLabel("No repair suggestions from current diagnostics. Run checks if something still fails."))
            self.repair_container.addStretch(1)
            return
        for label in suggestions:
            check = QCheckBox(label)
            check.setChecked(True)
            self.repair_checks[label] = check
            self.repair_container.addWidget(check)
        self.repair_container.addStretch(1)

    def _suggest_repairs(self, data: dict[str, Any]) -> list[str]:
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
        runtime_map = {"sam3": "Install/repair SAM3 venv", "matanyone2": "Install/repair MatAnyone2 venv", "videomama": "Install/repair VideoMaMa venv", "sam31": "Install/repair SAM3.1 venv"}
        for item in ai.get("runtimes", []):
            label = runtime_map.get(item.get("id"))
            if label and not item.get("installed"):
                suggestions.append(label)
        model_map = {"sam3_transformers": "Install/repair SAM3 model", "matanyone2": "Install/repair MatAnyone2 model", "videomama": "Install/repair VideoMaMa model", "sam31_sam3plus": "Install/repair SAM3.1 model"}
        for item in ai.get("models", []):
            label = model_map.get(item.get("id"))
            if label and not item.get("present"):
                suggestions.append(label)
        suggestions.append("Run installer checks and dependency validation")
        deduped: list[str] = []
        for suggestion in suggestions:
            if suggestion not in deduped:
                deduped.append(suggestion)
        return deduped

    def run_selected_repairs(self) -> None:
        selected = [label for label, check in self.repair_checks.items() if check.isChecked()]
        if not selected:
            QMessageBox.information(self, "No repairs selected", "Select at least one repair action.")
            return
        details = "\n".join(f"• {label}" for label in selected)
        if QMessageBox.question(self, "Run selected repairs", f"Run these repair actions?\n\n{details}") != QMessageBox.Yes:
            return
        self.tabs.setCurrentIndex(self.tabs.count() - 1)
        self.append_log("\n=== Selected repairs ===\n" + details + "\n")
        self.repair_queue = selected
        self._run_next_repair()

    def _run_next_repair(self) -> None:
        if not self.repair_queue:
            self.refresh_diagnostics(show_log=True)
            return
        label = self.repair_queue.pop(0)
        action, args = REPAIR_ACTIONS[label]
        self.run_action(action, *args, select_logs=True, auto_yes=True)

    def select_all_suggested(self) -> None:
        for check in self.repair_checks.values():
            check.setChecked(True)

    def clear_repairs(self) -> None:
        for check in self.repair_checks.values():
            check.setChecked(False)

    def _human_report(self) -> str:
        lines = ["Installation status:"]
        for row in range(self.status_table.rowCount()):
            area = self.status_table.item(row, 0).text()
            state = self.status_table.item(row, 1).text()
            details = self.status_table.item(row, 2).text()
            lines.append(f"  {state:4} {area}: {details}")
        return "\n".join(lines)

    def append_log(self, text: str) -> None:
        self.log.moveCursor(QTextCursor.End)
        self.log.insertPlainText(text)
        self.log.moveCursor(QTextCursor.End)

    def clear_log(self) -> None:
        self.log.clear()

    def copy_log(self) -> None:
        QApplication.clipboard().setText(self.log.toPlainText(), QClipboard.Clipboard)

    def save_log(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Save Flux installer log", str(FLUX_ROOT / "flux-installer.log"), "Log files (*.log);;Text files (*.txt);;All files (*)")
        if path:
            Path(path).write_text(self.log.toPlainText(), encoding="utf-8")

    def set_buttons_enabled(self, enabled: bool) -> None:
        for button in self.buttons:
            if button is not self.cancel_button:
                button.setEnabled(enabled)

    def closeEvent(self, event) -> None:  # noqa: N802 - Qt override name.
        if self.process is not None:
            self.cancel_action()
        event.accept()

    def _apply_theme(self) -> None:
        palette = QPalette()
        palette.setColor(QPalette.Window, QColor(COLORS["base"]))
        palette.setColor(QPalette.WindowText, QColor(COLORS["text"]))
        palette.setColor(QPalette.Base, QColor(COLORS["sunken"]))
        palette.setColor(QPalette.AlternateBase, QColor(COLORS["raised"]))
        palette.setColor(QPalette.Text, QColor(COLORS["text"]))
        palette.setColor(QPalette.Button, QColor(COLORS["raised"]))
        palette.setColor(QPalette.ButtonText, QColor(COLORS["text"]))
        palette.setColor(QPalette.Highlight, QColor(COLORS["accent"]))
        palette.setColor(QPalette.HighlightedText, QColor("#FFFFFF"))
        QApplication.instance().setPalette(palette)
        QApplication.instance().setStyleSheet(f"""
            QWidget {{ background: {COLORS['base']}; color: {COLORS['text']}; font-family: Inter, Noto Sans, Sans; font-size: 10.5pt; }}
            #LeftRail {{ background: #19191E; border-right: 1px solid #3A3A44; }}
            #Workspace {{ background: {COLORS['base']}; }}
            #Splash {{ background: {COLORS['sunken']}; border: 1px solid #3A3A44; border-radius: 14px; color: {COLORS['accent']}; font-size: 28pt; font-weight: 800; }}
            #HeroTitle {{ font-size: 26pt; font-weight: 800; color: white; }}
            #HeroSubtitle, #PageDetail, #ActionDetail {{ color: {COLORS['muted']}; }}
            #PageTitle {{ font-size: 18pt; font-weight: 750; color: white; }}
            #StatusCard, #ActionRow, QGroupBox {{ background: {COLORS['raised']}; border: 1px solid #41414B; border-radius: 12px; }}
            #StatusCard {{ min-height: 60px; }}
            #CardTitle {{ color: {COLORS['muted']}; font-size: 9pt; text-transform: uppercase; letter-spacing: 1px; }}
            #CardValue {{ font-size: 11pt; font-weight: 700; }}
            #ActionTitle {{ font-size: 12pt; font-weight: 700; color: white; }}
            #Recommendation {{ background: #242A35; border: 1px solid #385A92; border-radius: 10px; padding: 12px; font-weight: 700; }}
            #MonoBlock, #LogView {{ background: {COLORS['sunken']}; border: 1px solid #3A3A44; border-radius: 10px; padding: 10px; font-family: 'JetBrains Mono', 'Noto Sans Mono', monospace; }}
            QPushButton {{ background: {COLORS['raised']}; border: 1px solid #4A4A55; border-radius: 8px; padding: 8px 12px; font-weight: 700; }}
            QPushButton:hover {{ border-color: {COLORS['hover']}; color: white; }}
            QPushButton:pressed {{ background: {COLORS['sunken']}; }}
            QPushButton:disabled {{ color: #66666F; border-color: #33333A; }}
            #PrimaryButton {{ background: {COLORS['accent']}; color: white; border-color: {COLORS['accent']}; }}
            #LaunchButton {{ background: #2F5135; border-color: {COLORS['success']}; color: white; }}
            QTabWidget::pane {{ border: 1px solid #3A3A44; border-radius: 12px; top: -1px; }}
            QTabBar::tab {{ background: #24242B; padding: 10px 16px; border: 1px solid #3A3A44; border-bottom: none; border-top-left-radius: 8px; border-top-right-radius: 8px; }}
            QTabBar::tab:selected {{ background: {COLORS['raised']}; color: white; border-color: {COLORS['accent']}; }}
            QTableWidget {{ background: {COLORS['sunken']}; gridline-color: #33333A; border: 1px solid #3A3A44; border-radius: 10px; }}
            QHeaderView::section {{ background: {COLORS['raised']}; color: white; padding: 7px; border: 0; border-right: 1px solid #3A3A44; }}
            QLineEdit {{ background: {COLORS['sunken']}; border: 1px solid #454550; border-radius: 8px; padding: 8px; }}
            QCheckBox {{ spacing: 8px; padding: 4px; }}
            QProgressBar {{ background: {COLORS['sunken']}; border: 1px solid #3A3A44; border-radius: 6px; height: 10px; }}
            QProgressBar::chunk {{ background: {COLORS['accent']}; border-radius: 5px; }}
            #FooterStatus {{ color: {COLORS['muted']}; padding-left: 8px; }}
        """)


def main() -> int:
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        print("ERROR: cannot start graphical installer: no graphical display is available.", file=sys.stderr)
        print("Run tools/linux/flux-linux-setup.sh --tui for the terminal fallback.", file=sys.stderr)
        return 2
    app = QApplication(sys.argv)
    font = QFont("Noto Sans", 10)
    app.setFont(font)
    window = FluxInstallerWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
