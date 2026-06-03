# Flux Linux Workstation Setup

Flux setup is interactive-only. There are no installer arguments.

From a cloned repository, run exactly:

```bash
tools/linux/flux-linux-setup.sh
```

The installer opens an arrow-key guided menu: use Up/Down to review actions,
read each inline description, press Enter to run the selected action, or press
`q` to quit. It shows workstation status above the menu and asks before every
mutating step: submodule update, RPM Fusion/dependency installation, configure,
build, app install, Python runtime dependency installation, PyPlug/OpenFX/AI
payload deployment, scoped OFX cache clearing, launcher installation,
validation, launch, or uninstall.

The interactive menu includes explicit AI actions:

- Install/download AI models
- Enter/remember Hugging Face token securely
- Show AI model status
- Remove installed AI model

These AI menu actions bootstrap the required AI tools and Python dependencies if
missing, without rebuilding or reinstalling the Flux application. You can install
or remove only models at any time from the menu.

Model downloads show license, token, non-commercial, and GPL notices as
informational prompts. Hugging Face tokens are entered through hidden interactive
prompts and may be remembered only through an accepted secure desktop keyring.
Flux never stores plaintext tokens and never calls Hugging Face global login
tools.

Use environment variables for non-default paths before starting the installer,
for example `FLUX_INSTALL_PREFIX`, `FLUX_BIN_DIR`, `FLUX_BUILD_DIR`,
`PLUGIN_PREFIX`, `XDG_CACHE_HOME`, or `HOME`. Do not pass installer arguments.

Developer internals: lower-level shell functions exist inside the script for the
interactive menu and validation, but they are not public CLI entry points.
