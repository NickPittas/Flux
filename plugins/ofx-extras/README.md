# Flux Linux OFX Extras

Repository-local Linux x86-64 OFX bundle set required for Flux's restored
PyPlug/tool coverage.

These bundles are installed by:

```bash
./tools/linux/flux-linux-setup.sh --deploy-extras
```

Installed user path:

```text
~/.OFX/Plugins/
```

Required restored provider IDs:

| Bundle | Runtime IDs |
|---|---|
| `CImg.ofx.bundle` | `net.sf.cimg.CImgBlur`, `net.sf.cimg.CImgBloom`, `net.sf.cimg.CImgDilate` |
| `SeExpr.ofx.bundle` | `fr.inria.openfx.SeExpr`, `fr.inria.openfx.SeExprSimple`, `net.sf.openfx.SeNoise`, `net.sf.openfx.SeGrain` |
| `Text.ofx.bundle` | `net.fxarena.openfx.Text`, `net.fxarena.openfx.RichText` |
| `Magick.ofx.bundle` | `net.fxarena.openfx.Tile` |
| `ResolveMath.ofx.bundle` | `OpenFX.Yo.ResolveMath` |

`IO.ofx.bundle` and `Misc.ofx.bundle` are also staged here for transfer-package
completeness, but the canonical checkout copies those from `plugins/` first.

Validation:

```bash
./tools/linux/flux-linux-setup.sh --validate-ldd
```

After installing/replacing bundles, clear only the scoped OpenFX cache:

```bash
./tools/linux/flux-linux-setup.sh --clear-ofx-cache
```

Then launch Flux once to regenerate the runtime registry cache.
