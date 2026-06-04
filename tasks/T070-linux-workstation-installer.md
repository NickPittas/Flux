# T070 — Linux workstation installer

## Status
DONE

## Current rule
The Flux Linux installer is interactive-only. Run:

```bash
tools/linux/flux-linux-setup.sh
```

The script rejects every argument and opens an arrow-key interactive terminal
menu when run correctly. Up/Down moves selection, Enter runs the selected action,
and q quits. All setup, refresh, repair, validation, launch, and uninstall
choices are prompts inside that menu with inline help text.

## Current responsibilities
- Show workstation status.
- Ask before submodule update, RPM Fusion setup, dependency installation,
  configure, build, app/runtime deployment, Python runtime dependency bootstrap,
  AI payload/model setup, OFX cache clearing, launcher installation, validation,
  launch, and uninstall.
- Keep installed Flux payloads under the Flux install prefix.
- Deploy PyPlugs, OpenFX bundles, Natron OpenColorIO configs, AI tools, and the user launcher.
- Preserve secure-token policy: no plaintext Hugging Face token storage.

## Validation evidence
- Bash syntax validation passed.
- Natron OpenColorIO config archive download/extract command validated against the upstream GitHub tarball.
- Argument rejection was verified.
- Documentation was scrubbed so the installer is presented only as an
  interactive no-argument command.
