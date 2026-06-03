# Research: T083 secure token and bundled model install strategy on Linux/Fedora

## Summary
Flux should not use `huggingface_hub.login()` as the persistence mechanism, because Hugging Face persists tokens under `HF_HOME`/the Hub cache by default; instead, Flux should store any Hugging Face token in the desktop secret store and pass it to `huggingface_hub` only in-memory per download call. Recommended implementation: Qt/C++ runtime uses QtKeychain/libsecret/KWallet; Python installer/model manager uses `keyring` with Secret Service/KWallet backends; if no encrypted keyring is available, do not persist the token and require `--token-stdin`, an environment token, or interactive re-entry.

## Findings
1. **Hugging Face's normal login flow persists a token in the Hugging Face cache, which conflicts with “never plaintext.”** The Hub docs say `login()` persists the token in cache and can also add it to git credentials; environment docs say `HF_HOME` stores data including “your token” and defaults to `~/.cache/huggingface` unless `XDG_CACHE_HOME` is set. For Flux, avoid `hf auth login`/`huggingface_hub.login()` for installer persistence, and avoid `add_to_git_credential`. Use `snapshot_download(..., token=<token string>)` or `HfApi(token=...)` with a token loaded from the OS keyring at runtime. [HF auth docs](https://huggingface.co/docs/huggingface_hub/main/en/package_reference/authentication), [HF env vars](https://huggingface.co/docs/huggingface_hub/en/package_reference/environment_variables.md)

2. **Best Linux desktop secret-store abstraction for Flux C++/Qt is QtKeychain, with libsecret/Secret Service and KWallet coverage.** QtKeychain is a Qt API for securely storing passwords; on Linux/Unix it uses GNOME Keyring if running, tries KWallet via D-Bus if available, and supports libsecret. This matches Flux’s Qt6/Fedora/KDE target without inventing crypto or writing plaintext config. [QtKeychain README](https://github.com/frankosterfeld/qtkeychain/blob/master/ReadMe.md)

3. **Best Python-side installer/model-manager abstraction is `keyring`, but only with a secure backend accepted.** Python `keyring` supports Freedesktop Secret Service and KDE KWallet on Linux; however, backend availability varies, and weak/plaintext fallback backends exist in the broader ecosystem. Flux should explicitly inspect `keyring.get_keyring()` and accept only known encrypted backends such as Secret Service/libsecret, KWallet, or a user-selected secure backend; otherwise degrade to session-only token input. [keyring PyPI](https://pypi.org/project/keyring/), [keyring GitHub](https://github.com/jaraco/keyring/)

4. **KDE/Fedora path: Secret Service is increasingly the common target, while KWallet remains important.** KDE’s KWallet framework is “safe desktop-wide storage for passwords”; recent KDE architecture exposes/proxies Secret Service through KWallet/ksecretd, while legacy KWallet D-Bus APIs remain common. Flux on Fedora KDE should work with either Secret Service/libsecret or KWallet, but should prefer a generic Secret Service path where practical. [KDE KWallet](https://github.com/KDE/kwallet), [KWallet Secret Service transition](https://notmart.org/blog/2025/04/towards-a-transition-from-kwallet-to-secret-service/)

5. **Headless installers cannot assume a desktop keyring exists or is unlocked.** Secret Service/KWallet usually require a user session D-Bus and possibly wallet unlock UI. For headless/distrobox/CI/server installs, the secure choices are: prompt without echo and use token only for the current download; accept token from stdin (`--hf-token-stdin`) or environment (`HF_TOKEN`/`FLUX_HF_TOKEN`) for one-shot use; or skip gated downloads and instruct the user to run `flux-model-manager login` later from the desktop session. Do not write an “encrypted with local file key” fallback unless Nick explicitly approves the UX/security tradeoff.

6. **Hugging Face model caches should be controlled and path-agnostic.** `HF_HOME` controls the Hub home and defaults to `~/.cache/huggingface`; `HF_HUB_CACHE` controls the repository cache; `snapshot_download()` supports `cache_dir`, `local_dir`, `local_files_only`, `allow_patterns`, `ignore_patterns`, revision pinning, and token arguments. Flux should set/cache under XDG paths, e.g. `${XDG_CACHE_HOME:-$HOME/.cache}/Flux/huggingface` for shared Hub cache and `${XDG_DATA_HOME:-$HOME/.local/share}/Flux/models/<model-id>/<revision>` for installed, manifest-tracked model snapshots. [HF env vars](https://huggingface.co/docs/huggingface_hub/en/package_reference/environment_variables.md), [HF downloading files](https://huggingface.co/docs/huggingface_hub/main/package_reference/file_download), [snapshot_download source/API](https://github.com/huggingface/huggingface_hub/blob/main/src/huggingface_hub/_snapshot_download.py)

7. **Gated model licenses require an explicit web-side acceptance step; a token alone is not enough.** Hugging Face gated models require individual user access/approval and may require the user to share contact information with the model author. Flux installer should detect 401/403/gated errors, show the model page/license URL, and ask the user to accept/request access in a browser before retrying. [HF gated models](https://huggingface.co/docs/hub/models-gated)

8. **Offline bundles are the cleanest path for production installs and avoid token storage entirely.** Flux can ship or separately provide model bundles as tar/zstd archives containing pinned snapshots plus `flux-model-manifest.json` with model id, revision/commit SHA, files, hashes, license metadata, source URL, and expected runtime. The installer should verify hashes, install to XDG data, and configure runtime to use `local_files_only=True` when offline. For Hugging Face-sourced bundles, generate them with `snapshot_download(revision=<commit>, allow_patterns=...)` and never include a user token inside the bundle.

## Concrete recommended strategy for Flux

1. **Split model acquisition from app install.** Base installer installs Flux and the model manager. AI models are optional install profiles: `none`, `recommended`, `full`, or `offline-bundle <path>`.

2. **Token capture UX.**
   - GUI/desktop: if a selected model needs a Hugging Face token, show a Flux dialog explaining why, link to the HF token page/model license page, password-mask the input, and offer “remember securely in desktop keyring.”
   - Terminal/headless: support `--hf-token-stdin`, `--hf-token-env FLUX_HF_TOKEN`, and hidden prompt via `getpass`; default to session-only unless a secure keyring backend is detected and `--remember-token` is explicit.

3. **Secure persistence.**
   - Runtime C++/Qt: use QtKeychain service `org.flux.Flux`, key `huggingface.token`.
   - Python installer/model manager: use `keyring.set_password("org.flux.Flux", "huggingface.token", token)` only if backend is Secret Service/libsecret/KWallet or another explicitly allowed encrypted backend.
   - Never persist token in Flux config, project files, logs, shell history, Hugging Face cache token files, or git credentials.
   - Redact tokens in all command output; pass token via Python API argument, not command-line argv.

4. **Hugging Face API usage.**
   - Do not call `huggingface_hub.login()` or `hf auth login` in Flux automation.
   - Use `snapshot_download(repo_id, revision=<pinned commit>, token=token, cache_dir=flux_hf_cache, local_dir=staging_dir, allow_patterns=...)`.
   - After download, verify manifest hashes and copy/atomically rename into Flux model store.
   - For runtime loading, prefer local installed paths and `local_files_only=True`; only the model manager should touch the network.

5. **Fallback policy.**
   - If secure keyring unavailable/unlocked: allow one-shot token use only, then discard from memory; print “token not stored because no encrypted desktop secret service is available.”
   - If no token supplied: install only public/offline models; mark gated models as “requires login/license acceptance.”
   - Do not implement plaintext fallback. Do not implement home-grown reversible encryption using a key stored beside the ciphertext.

6. **Cache/install locations.**
   - Hub download cache: `${XDG_CACHE_HOME:-$HOME/.cache}/Flux/huggingface`.
   - Staging downloads: `${XDG_CACHE_HOME:-$HOME/.cache}/Flux/model-downloads/<tmp>`.
   - Installed models: `${XDG_DATA_HOME:-$HOME/.local/share}/Flux/models/<provider>/<model>/<revision>`.
   - Config/manifest: `${XDG_CONFIG_HOME:-$HOME/.config}/Flux/models.json` with no secrets.
   - Offline bundle import should work without network and without token.

7. **License gates and auditability.**
   - Model manifest must include license, upstream URL, revision, and whether gated access was required.
   - Installer must require explicit user confirmation for non-redistributable/gated models and should not bundle gated weights unless redistribution license permits it.
   - For gated HF errors, open/show the HF model page and retry after acceptance.

8. **Implementation shape.**
   - Add a small `flux-model-manager` Python helper for install/update/list/remove/verify/login/logout.
   - Add a Qt runtime secret adapter using QtKeychain for future GUI model downloads.
   - Keep inference runtime independent from Hugging Face auth: it receives resolved local model paths only.

## Sources
- Kept: Hugging Face authentication docs (https://huggingface.co/docs/huggingface_hub/main/en/package_reference/authentication) — confirms `login()` persists tokens and may touch git credentials.
- Kept: Hugging Face environment variables (https://huggingface.co/docs/huggingface_hub/en/package_reference/environment_variables.md) — documents `HF_HOME`, `HF_HUB_CACHE`, default cache/token location behavior, and `HF_TOKEN` override.
- Kept: Hugging Face downloading files (https://huggingface.co/docs/huggingface_hub/main/package_reference/file_download) — documents download/cache/local_dir behavior.
- Kept: Hugging Face gated models (https://huggingface.co/docs/hub/models-gated) — primary source for gated access/license semantics.
- Kept: QtKeychain README (https://github.com/frankosterfeld/qtkeychain/blob/master/ReadMe.md) — directly relevant Qt/C++ secret storage abstraction.
- Kept: Python keyring docs/PyPI (https://pypi.org/project/keyring/) — directly relevant installer-side secret storage abstraction.
- Kept: KDE KWallet (https://github.com/KDE/kwallet) — primary KDE credential storage source.
- Dropped: systemd-cryptsetup/systemd-creds sources — useful for system services, but not appropriate for per-user desktop app tokens.
- Dropped: ArchWiki KWallet — useful background, but not primary enough for the recommendation.
- Dropped: random GitHub issues/PRs on keyring/libsecret — lower authority than project docs for this decision.

## Gaps
- Need local validation on Fedora 44 KDE: confirm which backend Python `keyring` selects by default, whether KWallet prompts correctly under Wayland/xcb, and whether QtKeychain links cleanly in the existing CMake/Qt6 build.
- Need legal/product decision per T083 model: which weights may be redistributed in Flux offline bundles, which must be user-downloaded, and what license text must be shown.
- Need final model list before exact `allow_patterns`, disk-size estimates, and bundle manifests can be defined.
