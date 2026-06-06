#!/usr/bin/env python3
"""Flux AI model manager: install/verify/remove/list/status with secure HF token use."""
from __future__ import annotations
import argparse, getpass, json, os, select, shutil, subprocess, sys, tempfile, termios, tty, urllib.request
from pathlib import Path
from typing import Any, Dict, Iterable, List

APP_DIR="Flux"; SERVICE="org.flux.Flux"; TOKEN_USER="huggingface.token"
REQUIRED_TOP_LEVEL={"manifest_version","models"}
REQUIRED_MODEL_FIELDS={"id","display_name","category","role","priority","provider","runtime_type","install_policy","bundling_policy","license_summary","warning_text","gated_token_requirement","default_enabled","default_installable","implemented","output_use_notes","source"}

def redact(s:str|None)->str:
    if not s: return ""
    return "<redacted>" if len(s)>8 else "<redacted>"

def xdg_home(env, suffix): return Path(os.environ.get(env) or (Path.home()/suffix)).expanduser()
def flux_paths():
    data=xdg_home("XDG_DATA_HOME",".local/share"); cache=xdg_home("XDG_CACHE_HOME",".cache"); config=xdg_home("XDG_CONFIG_HOME",".config")
    return {"data_home":data,"cache_home":cache,"config_home":config,"model_store":data/APP_DIR/"models","hub_cache":cache/APP_DIR/"huggingface","download_staging":cache/APP_DIR/"model-downloads","config_file":config/APP_DIR/"models.json"}
def default_manifest_path(): return Path(__file__).resolve().with_name("model_manifest.json")
def load_manifest(path:Path):
    with path.open("r",encoding="utf-8") as h: data=json.load(h)
    if not isinstance(data,dict): raise ValueError("manifest root must be an object")
    return data

def validate_manifest(data):
    errors=[]; missing=REQUIRED_TOP_LEVEL-set(data)
    if missing: errors.append("manifest missing top-level fields: "+", ".join(sorted(missing)))
    models=data.get("models")
    if not isinstance(models,list): return errors+["manifest field 'models' must be a list"]
    seen=set()
    for i,m in enumerate(models):
        p=f"models[{i}]"
        if not isinstance(m,dict): errors.append(f"{p} must be an object"); continue
        miss=REQUIRED_MODEL_FIELDS-set(m)
        if miss: errors.append(f"{p} missing fields: {', '.join(sorted(miss))}")
        mid=m.get("id")
        if not isinstance(mid,str) or not mid: errors.append(f"{p}.id must be a non-empty string")
        elif mid in seen: errors.append(f"duplicate model id: {mid}")
        else: seen.add(mid)
        if not isinstance(m.get("priority"),int): errors.append(f"{p}.priority must be an integer")
        for flag in ("default_enabled","default_installable"):
            if not isinstance(m.get(flag),bool): errors.append(f"{p}.{flag} must be boolean")
        if not isinstance(m.get("source"),dict): errors.append(f"{p}.source must be an object")
    return errors

def model_is_implemented(m): return bool(m.get("implemented"))
def sorted_models(data, include_unimplemented=False):
    rows = data.get("models", [])
    if not include_unimplemented:
        rows = [m for m in rows if model_is_implemented(m)]
    return sorted(rows, key=lambda m:(-int(m.get("priority",0)), str(m.get("id",""))))
def find_model(data, mid):
    for m in data.get("models",[]):
        if m.get("id")==mid: return m
    raise ValueError(f"unknown model: {mid}")
def warning_labels(m):
    c=" ".join(str(m.get(k,"")) for k in ("install_policy","bundling_policy","gated_token_requirement","license_summary")).lower(); labels=[]
    if "gated" in c or "token" in c: labels.append("GATED/TOKEN")
    if "noncommercial" in c or "non-commercial" in c or "restrictive" in c: labels.append("NON-COMMERCIAL/RESTRICTIVE")
    if "gpl3" in c or "external_helper" in c: labels.append("EXTERNAL GPL3 HELPER")
    return labels

def model_local_path(m, paths):
    s=m.get("source",{}) if isinstance(m.get("source"),dict) else {}; rev=s.get("revision") or "unversioned"
    return paths["model_store"]/str(m.get("id"))/str(rev)
def is_installed(m, paths): return (model_local_path(m,paths)/"flux_model_install.json").is_file()

def cmd_list(args):
    data=load_manifest(args.manifest)
    include_unimplemented = bool(getattr(args, "include_unimplemented", False))
    if getattr(args,"json",False):
        paths=flux_paths()
        rows=[]
        for m in sorted_models(data, include_unimplemented=include_unimplemented):
            rows.append({"id":m["id"],"display_name":m.get("display_name",""),"role":m.get("role",""),"license_summary":m.get("license_summary",""),"warning_text":m.get("warning_text",""),"default_enabled":bool(m.get("default_enabled")),"default_installable":bool(m.get("default_installable")),"implemented":bool(m.get("implemented")),"installed":is_installed(m,paths),"labels":warning_labels(m)})
        print(json.dumps({"models":rows},sort_keys=True))
        return 0
    for m in sorted_models(data, include_unimplemented=include_unimplemented):
        labels=warning_labels(m); print(f"{m['id']}: {m['display_name']}"+(f" [{' | '.join(labels)}]" if labels else ""))
        print(f"  role: {m['role']}\n  install: {m['install_policy']} | bundle: {m['bundling_policy']}\n  license: {m['license_summary']}")
        if m.get("warning_text"): print(f"  warning: {m['warning_text']}")
    return 0

def cmd_verify(args):
    data=load_manifest(args.manifest); errors=validate_manifest(data)
    if errors:
        print("Manifest verification FAILED",file=sys.stderr); [print("ERROR: "+e,file=sys.stderr) for e in errors]; return 1
    if args.offline: print("Manifest verification OK (offline); no network performed."); return 0
    paths=flux_paths(); bad=0
    for m in sorted_models(data, include_unimplemented=True):
        if is_installed(m,paths): print(f"OK installed: {m['id']} -> {model_local_path(m,paths)}")
    return bad

def cmd_status(args):
    data=load_manifest(args.manifest); paths=flux_paths(); print("Flux AI model paths:")
    for k in ("model_store","hub_cache","download_staging","config_file"): print(f"  {k}: {paths[k]} (exists: {paths[k].exists()})")
    for m in sorted_models(data, include_unimplemented=True): print(f"  {m['id']}: {'installed' if is_installed(m,paths) else 'missing'} ({model_local_path(m,paths)})")
    return 0

SECURE_KEYRING_BACKENDS={
    "keyring.backends.SecretService.Keyring",
    "keyring.backends.libsecret.Keyring",
    "keyring.backends.kwallet.DBusKeyring",
    "keyring.backends.kwallet.DBusKeyringKWallet4",
    "keyring.backends.kwallet.DBusKeyringKWallet5",
}
INSECURE_KEYRING_NAME_PARTS=("plaintext","fail","null","test","file")

def keyring_backend_name(backend)->str:
    return backend.__class__.__module__+"."+backend.__class__.__name__

def is_secure_keyring_backend(backend)->bool:
    name=keyring_backend_name(backend)
    low=name.lower()
    if any(part in low for part in INSECURE_KEYRING_NAME_PARTS): return False
    return name in SECURE_KEYRING_BACKENDS

def secure_keyring():
    try:
        import keyring, keyring.backend
        current=keyring.get_keyring()
        if is_secure_keyring_backend(current): return keyring
        secure_backends=[b for b in keyring.backend.get_all_keyring() if is_secure_keyring_backend(b)]
        if not secure_backends: return None
        secure_backends.sort(key=lambda b: getattr(b,"priority",0), reverse=True)
        keyring.set_keyring(secure_backends[0])
        return keyring
    except Exception: return None

def keyring_token():
    kr=secure_keyring()
    if not kr: return None
    try: return kr.get_password(SERVICE,TOKEN_USER)
    except Exception: return None

def prompt_yes_no(prompt:str, default:bool=False)->bool:
    if not sys.stdin.isatty() or not sys.stdout.isatty(): return default
    suffix=" [Y/n]: " if default else " [y/N]: "
    answer=input(prompt+suffix).strip().lower()
    if not answer: return default
    return answer in {"y","yes"}

def remember_token_securely(tok:str)->None:
    if not prompt_yes_no("Remember this token in the desktop secure keyring?", False): return
    kr=secure_keyring()
    if not kr:
        print("No accepted secure keyring backend is available; refusing plaintext persistence. Continuing with this one-shot token only.")
        return
    try:
        kr.set_password(SERVICE,TOKEN_USER,tok)
        print("Hugging Face token stored in secure keyring.")
    except Exception as e:
        print(f"Secure keyring persistence failed; refusing plaintext fallback. Continuing one-shot only: {e}", file=sys.stderr)

def token_from_args(args, needed=False):
    cached=getattr(args,"_hf_token",None)
    if cached: return cached
    tok=os.environ.get("FLUX_HF_TOKEN")
    if tok: return tok
    if getattr(args,"token_stdin",False): return sys.stdin.read().strip()
    tok=keyring_token()
    if tok: return tok
    if needed and sys.stdin.isatty() and not getattr(args,"yes",False):
        tok=getpass.getpass("Hugging Face token (input hidden): ").strip()
        if tok:
            setattr(args,"_hf_token",tok)
            remember_token_securely(tok)
        return tok or None
    return None

def write_meta(m,path,files):
    meta={"model_id":m["id"],"display_name":m.get("display_name"),"source":m.get("source"),"files":files,"warnings":m.get("warning_text"),"license_summary":m.get("license_summary")}
    (path/"flux_model_install.json").write_text(json.dumps(meta,indent=2,sort_keys=True),encoding="utf-8")

def _prepare_install_dir(dest:Path, force:bool)->Path:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if force and dest.exists():
        shutil.rmtree(dest)
    staging = dest.parent / f".{dest.name}.partial"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True, exist_ok=True)
    return staging

def _finalize_install_dir(staging:Path, dest:Path)->None:
    if dest.exists():
        shutil.rmtree(dest)
    os.replace(staging, dest)

def _cleanup_install_dir(staging:Path)->None:
    if staging.exists():
        shutil.rmtree(staging, ignore_errors=True)

def install_hf(m,args,paths):
    s=m["source"]; req=str(m.get("gated_token_requirement","")).lower(); need=req.startswith("required") or req in {"token_required", "required_for_download"}; tok=token_from_args(args,need)
    if need and not tok:
        if sys.stdin.isatty() and not getattr(args,"yes",False):
            print(f"SKIP {m['id']}: gated Hugging Face model requires access/token and no token was entered.")
        else:
            print(f"SKIP {m['id']}: gated Hugging Face model requires access/token. Interactive installer prompts when run from a terminal; advanced one-shot paths are FLUX_HF_TOKEN or --token-stdin.")
        return 2
    if args.dry_run: print(f"DRY-RUN HF {m['id']}: repo={s['repo']} revision={s['revision']} allow={s.get('allow_patterns')}"); return 0
    try: from huggingface_hub import snapshot_download
    except Exception: print("ERROR: install requires huggingface_hub (installer bootstraps it for normal Flux installs).",file=sys.stderr); return 1
    dest=model_local_path(m,paths); paths["hub_cache"].mkdir(parents=True,exist_ok=True); staging=_prepare_install_dir(dest,args.force)
    try:
        snapshot_download(repo_id=s["repo"], revision=s.get("revision"), token=tok, cache_dir=str(paths["hub_cache"]), local_dir=str(staging), allow_patterns=s.get("allow_patterns"))
        files=[str(p.relative_to(staging)) for p in staging.rglob("*") if p.is_file() and p.name!="flux_model_install.json"]
        write_meta(m,staging,files)
        _finalize_install_dir(staging,dest)
    except Exception:
        _cleanup_install_dir(staging)
        raise
    print(f"Installed {m['id']} -> {dest}"); return 0
def install_direct(m,args,paths):
    s=m["source"]; urls=s.get("urls") or ([s.get("url")] if s.get("url") else [])
    if args.dry_run: print(f"DRY-RUN URL {m['id']}: "+", ".join(u.get('url',u) if isinstance(u,dict) else u for u in urls)); return 0
    dest=model_local_path(m,paths); dest.mkdir(parents=True,exist_ok=True); files=[]
    dest=model_local_path(m,paths); staging=_prepare_install_dir(dest,args.force); files=[]
    try:
        for item in urls:
            url=item.get("url") if isinstance(item,dict) else item; name=item.get("filename") if isinstance(item,dict) else Path(url).name
            print(f"Downloading {m['id']} file {name}"); urllib.request.urlretrieve(url, staging/name); files.append(name)
        write_meta(m,staging,files)
        _finalize_install_dir(staging,dest)
    except Exception:
        _cleanup_install_dir(staging)
        raise
    print(f"Installed {m['id']} -> {dest}"); return 0
def _candidate_paths(source, kind):
    for item in source.get("local_candidates", []) or []:
        if not isinstance(item, dict) or item.get("kind") != kind:
            continue
        env_name = item.get("path_env")
        if env_name:
            env_path = os.environ.get(env_name)
            if env_path:
                yield Path(env_path).expanduser()
        raw = item.get("path")
        if raw:
            yield Path(raw).expanduser()

def _copy_if_exists(patterns, src_dir, dst_dir):
    copied=[]
    for pat in patterns:
        for p in src_dir.glob(pat):
            if p.is_file():
                dst_dir.mkdir(parents=True, exist_ok=True)
                target = (dst_dir / p.name).resolve()
                src = p.resolve()
                if src != target:
                    shutil.copy2(src, target)
                copied.append(target.name)
    return copied

def _which(executable:str)->Path|None:
    found=shutil.which(executable)
    return Path(found) if found else None

def _run(cmd:list[str], cwd:Path|None=None, env:dict[str,str]|None=None)->None:
    subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, check=True)

def _ensure_git_checkout(url:str, dest:Path)->None:
    if dest.exists():
        _run(["git","-C",str(dest),"fetch","--depth","1","origin"])
        _run(["git","-C",str(dest),"reset","--hard","origin/HEAD"])
    else:
        dest.parent.mkdir(parents=True, exist_ok=True)
        _run(["git","clone","--depth","1",url,str(dest)])

def _download_once(url:str, dest:Path)->None:
    if dest.is_file() and dest.stat().st_size > 0:
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(url, dest)

def _find_trtexec(source:dict)->Path|None:
    env_trtexec = os.environ.get("FLUX_TRTEXEC")
    if env_trtexec:
        p = Path(env_trtexec).expanduser()
        if p.is_file():
            return p
    for candidate in _candidate_paths(source, "trtexec_bin"):
        if candidate.is_file():
            return candidate
        if candidate.is_dir():
            p = candidate / "trtexec"
            if p.is_file():
                return p
    return _which("trtexec")

def _find_tensorrt_lib_dir(source:dict, trtexec:Path|None)->Path|None:
    for candidate in _candidate_paths(source, "tensorrt_lib_dir"):
        if candidate.is_dir() and ((candidate / "libnvinfer.so").exists() or any(candidate.glob("libnvinfer.so*"))):
            return candidate
    if trtexec:
        root = trtexec.parent.parent
        for rel in ("lib","lib64"):
            candidate = root / rel
            if candidate.is_dir() and ((candidate / "libnvinfer.so").exists() or any(candidate.glob("libnvinfer.so*"))):
                return candidate
    return None

def _stage_tensorrt_libs(source:dict, dest:Path, staged:list[str], trtexec:Path|None)->None:
    lib_dest = dest / "tensorrt" / "lib"
    matched_lib_dir = _find_tensorrt_lib_dir(source, trtexec)
    if matched_lib_dir is not None:
        staged.extend(_copy_if_exists(["libnvinfer.so*", "libnvinfer_plugin.so*", "libnvonnxparser.so*"], matched_lib_dir, lib_dest))
    else:
        print("WARNING: TensorRT runtime libraries were not found in known local locations. Installed Flux will still require TensorRT runtime libraries from the system or FLUX_TENSORRT_LIB_DIR.", file=sys.stderr)

def _stage_local_corridorkey_assets(source:dict, dest:Path, required:list[str], staged:list[str])->bool:
    engine_dirs = list(_candidate_paths(source, "engine_dir"))
    matched_engine_dir = None
    for candidate in engine_dirs:
        if all((candidate / name).is_file() for name in required):
            matched_engine_dir = candidate
            break
    if matched_engine_dir is None:
        return False
    for name in required:
        src = (matched_engine_dir / name).resolve()
        dst = (dest / name).resolve()
        if src != dst:
            shutil.copy2(src, dst)
        staged.append(name)
    trtexec = _find_trtexec(source)
    _stage_tensorrt_libs(source, dest, staged, trtexec)
    return True

def _build_corridorkey_assets(source:dict, dest:Path, required:list[str], paths:dict, force:bool)->list[str]:
    builder = source.get("builder", {})
    work = paths["download_staging"] / "corridorkey-builder"
    corridor = work / "CorridorKey"
    nuke = work / "CorridorKey-for-Nuke"
    weights_dir = corridor / "weights"
    staged:list[str] = []
    trtexec = _find_trtexec(source)
    if trtexec is None:
        raise RuntimeError("TensorRT builder tool trtexec is missing. Install/stage TensorRT and set FLUX_TRTEXEC if needed.")
    if _find_tensorrt_lib_dir(source, trtexec) is None:
        raise RuntimeError("TensorRT runtime libraries are missing. Stage TensorRT libs or set FLUX_TENSORRT_LIB_DIR.")
    if _which("git") is None:
        raise RuntimeError("git is required to acquire CorridorKey sources.")
    if _which("uv") is None:
        raise RuntimeError("uv is required to build CorridorKey engines automatically.")

    _ensure_git_checkout(builder["corridorkey_repo"], corridor)
    _ensure_git_checkout(builder["corridorkey_for_nuke_repo"], nuke)
    weights_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(nuke / "export_corridorkey_onnx.py", corridor / "export_corridorkey_onnx.py")
    _download_once(builder["weights_url"], weights_dir / builder.get("weights_filename", "CorridorKey_v1.0.pth"))

    python_hint = builder.get("python_hint") or "3.13"
    _run(["uv","sync","--python",python_hint,"--no-dev"], cwd=corridor)
    _run(["uv","pip","install","onnx","onnxscript"], cwd=corridor)

    trt_lib_dir = _find_tensorrt_lib_dir(source, trtexec)
    env = os.environ.copy()
    if trt_lib_dir:
        env["LD_LIBRARY_PATH"] = f"{trt_lib_dir}{os.pathsep}{env.get('LD_LIBRARY_PATH','')}".rstrip(os.pathsep)

    for size in builder.get("engine_sizes", [1024]):
        onnx = weights_dir / f"CorridorKey_v1.0_{size}.onnx"
        engine = weights_dir / f"CorridorKey_v1.0_{size}_fp16.engine"
        if force or not onnx.is_file():
            _run(["uv","run","python","export_corridorkey_onnx.py","--checkpoint",str(weights_dir / builder.get("weights_filename", "CorridorKey_v1.0.pth")),"--output",str(onnx),"--img-size",str(size),"--no-verify"], cwd=corridor, env=env)
        if force or not engine.is_file():
            _run([str(trtexec),"--onnx="+str(onnx),"--saveEngine="+str(engine),"--fp16","--memPoolSize=workspace:2G"], cwd=corridor, env=env)
        shutil.copy2(engine, dest / engine.name)
        staged.append(engine.name)

    _stage_tensorrt_libs(source, dest, staged, trtexec)
    return staged




def validate_required_files(m,path):
    required=[]
    payload=m.get("multi_source_payload")
    if isinstance(payload,dict):
        required.extend(str(x) for x in payload.get("required_files",[]) if x)
    source=m.get("source",{})
    if isinstance(source,dict):
        required.extend(str(x) for x in source.get("required_files",[]) if x)
    missing=[rel for rel in sorted(set(required)) if not (path/rel).is_file()]
    if missing:
        raise ValueError(f"{m['id']} install is incomplete; missing required files: {', '.join(missing)}")

def copy_bundled_files(m,dest):
    source=m.get("source",{})
    if not isinstance(source,dict):
        return
    for item in source.get("bundled_files",[]) or []:
        rel=str(item.get("path","")) if isinstance(item,dict) else str(item)
        if not rel:
            continue
        src=Path(__file__).resolve().parent/rel
        if not src.is_file():
            raise ValueError(f"{m['id']} bundled file missing from installer payload: {rel}")
        target=dest/Path(rel).name
        shutil.copy2(src,target)


def install_composite(m,args,paths):
    s=m["source"]
    steps=s.get("steps",[])
    if not isinstance(steps,list) or not steps:
        raise ValueError(f"{m['id']} composite source has no steps")
    if args.dry_run:
        print(f"DRY-RUN COMPOSITE {m['id']}:")
        for step in steps:
            print(f"  - {step.get('type')}: {step.get('repo') or step.get('url')} -> {step.get('target_subdir','.')}")
        for item in s.get("bundled_files",[]) or []:
            rel=item.get("path") if isinstance(item,dict) else item
            print(f"  - bundled: {rel} -> .")
        return 0
    try:
        from huggingface_hub import snapshot_download
    except Exception:
        print("ERROR: composite install requires huggingface_hub.",file=sys.stderr)
        return 1
    req=str(m.get("gated_token_requirement","")).lower()
    need=req.startswith("required") or req in {"token_required","required_for_download"}
    tok=token_from_args(args,need)
    dest=model_local_path(m,paths)
    staging=_prepare_install_dir(dest,args.force)
    paths["hub_cache"].mkdir(parents=True,exist_ok=True)
    try:
        for step in steps:
            typ=step.get("type")
            target=staging/str(step.get("target_subdir") or ".")
            target.mkdir(parents=True,exist_ok=True)
            if typ=="huggingface":
                print(f"Downloading {m['id']} component {step.get('role',step.get('repo'))}")
                snapshot_download(repo_id=step["repo"], revision=step.get("revision"), token=tok, cache_dir=str(paths["hub_cache"]), local_dir=str(target), allow_patterns=step.get("allow_patterns"))
            elif typ=="url":
                url=step["url"]; name=step.get("filename") or Path(url).name
                print(f"Downloading {m['id']} file {name}")
                urllib.request.urlretrieve(url, target/name)
            else:
                raise ValueError(f"{m['id']} unsupported composite step type: {typ}")
        copy_bundled_files(m,staging)
        validate_required_files(m,staging)
        files=[str(p.relative_to(staging)) for p in staging.rglob("*") if p.is_file() and p.name!="flux_model_install.json"]
        write_meta(m,staging,files)
        _finalize_install_dir(staging,dest)
    except Exception:
        _cleanup_install_dir(staging)
        raise
    print(f"Installed {m['id']} -> {dest}")
    return 0

def _provider_runtime_manifest_path()->Path:
    return Path(__file__).resolve().with_name("provider_runtime_manifest.json")

def _load_provider_runtime_manifest()->dict[str,Any]:
    path = _provider_runtime_manifest_path()
    if not path.is_file():
        return {"runtimes":[]}
    return json.loads(path.read_text(encoding="utf-8"))

def _runtime_for_model(model_id:str)->str|None:
    data = _load_provider_runtime_manifest()
    for runtime in data.get("runtimes", []) or []:
        if model_id in (runtime.get("models") or []):
            return str(runtime.get("id"))
    return None

def _system_ram_gib()->float:
    try:
        pages = os.sysconf("SC_PHYS_PAGES")
        page_size = os.sysconf("SC_PAGE_SIZE")
        return float(pages * page_size) / (1024 ** 3)
    except Exception:
        return 0.0

def _disk_free_gib(path:Path)->float:
    usage = shutil.disk_usage(path)
    return float(usage.free) / (1024 ** 3)

def _gpu_vram_gib()->float:
    nvidia_smi = _which("nvidia-smi")
    if not nvidia_smi:
        return 0.0
    try:
        out = subprocess.check_output([str(nvidia_smi), "--query-gpu=memory.total", "--format=csv,noheader,nounits"], text=True).strip().splitlines()
        return max((float(x.strip()) / 1024.0 for x in out if x.strip()), default=0.0)
    except Exception:
        return 0.0

def _corridorkey_preflight(source:dict, paths:dict)->None:
    missing = [tool for tool in ("git", "uv") if _which(tool) is None]
    trtexec = _find_trtexec(source)
    if trtexec is None:
        missing.append("trtexec")
    if _find_tensorrt_lib_dir(source, trtexec) is None:
        missing.append("TensorRT runtime libraries")
    if missing:
        raise RuntimeError("Missing CorridorKey prerequisites: " + ", ".join(missing))
    ram_gib = _system_ram_gib()
    paths["download_staging"].parent.mkdir(parents=True, exist_ok=True)
    disk_gib = _disk_free_gib(paths["download_staging"].parent)
    vram_gib = _gpu_vram_gib()
    if ram_gib and ram_gib < 120.0:
        raise RuntimeError(f"CorridorKey engine build needs about 128-140 GiB RAM for the 2048 export path; detected {ram_gib:.1f} GiB.")
    if disk_gib < 10.0:
        raise RuntimeError(f"CorridorKey engine build needs more free disk space; detected {disk_gib:.1f} GiB free.")
    if vram_gib and vram_gib < 20.0:
        raise RuntimeError(f"CorridorKey 2048 engine build expects about 24 GiB GPU VRAM; detected {vram_gib:.1f} GiB.")

def install_external(m,args,paths):
    source = m.get("source", {})
    mode = source.get("mode")
    if mode != "corridorkey_local_stage":
        print(f"ERROR: {m['id']} has unsupported external mode {mode}", file=sys.stderr)
        return 1
    if args.dry_run:
        print(f"DRY-RUN EXTERNAL {m['id']}: stage local engines if present, otherwise auto-acquire sources/weights and build engines with TensorRT.")
        return 0
    dest = model_local_path(m, paths)
    staging = _prepare_install_dir(dest, args.force)
    required = [str(x) for x in source.get("required_files", []) if x]
    staged:list[str] = []
    try:
        if not _stage_local_corridorkey_assets(source, staging, required, staged):
            _corridorkey_preflight(source, paths)
            staged.extend(_build_corridorkey_assets(source, staging, required, paths, args.force))
        write_meta(m, staging, sorted(set(staged)))
        validate_required_files(m, staging)
        _finalize_install_dir(staging, dest)
    except subprocess.CalledProcessError as e:
        _cleanup_install_dir(staging)
        print(f"ERROR: CorridorKey build step failed: {' '.join(e.cmd)}", file=sys.stderr)
        return 1
    except Exception as e:
        _cleanup_install_dir(staging)
        print(f"ERROR: CorridorKey install failed: {e}", file=sys.stderr)
        return 1
    print(f"Installed {m['id']} -> {dest}")
    return 0

def _ensure_runtime_for_model(model_id:str)->None:
    runtime_id = _runtime_for_model(model_id)
    if not runtime_id:
        return
    manager = Path(__file__).resolve().with_name("flux_provider_runtime.py")
    _run([sys.executable, str(manager), "install", runtime_id, "--cuda", os.environ.get("FLUX_AI_RUNTIME_CUDA","cu128"), "--json"])

def install_one(m,args,paths):
    typ=m.get("source",{}).get("type")
    req=str(m.get("gated_token_requirement","")).lower()
    needs_token=(typ in {"huggingface","huggingface_composite"}) and (req.startswith("required") or req in {"token_required","required_for_download"})
    if needs_token and not token_from_args(args, True):
        print(f"SKIP {m['id']}: gated Hugging Face model requires access/token. Accept access on Hugging Face, then provide FLUX_HF_TOKEN, --token-stdin, or the GUI token field.", file=sys.stderr)
        return 2
    if not m.get("default_installable",False) and not args.force:
        print(f"SKIP {m['id']}: not installable by default (use --force).",file=sys.stderr)
        return 1
    if not model_is_implemented(m) and not args.force:
        print(f"SKIP {m['id']}: model is not implemented in Flux and is hidden from normal installer flows (use --force only for manual recovery/remove).", file=sys.stderr)
        return 1
    if m.get("warning_text") and not (args.yes or args.dry_run):
        print("WARNING: "+m["warning_text"])
        input("Press Enter to continue or Ctrl-C to abort.")
    if not args.dry_run:
        _ensure_runtime_for_model(m["id"])
    if typ=="huggingface": return install_hf(m,args,paths)
    if typ=="huggingface_composite": return install_composite(m,args,paths)
    if typ=="url": return install_direct(m,args,paths)
    if typ=="external": return install_external(m,args,paths)
    print(f"ERROR: {m['id']} has unsupported source type {typ}",file=sys.stderr)
    return 1

def describe_install_selection(models):
    print("Flux AI model setup will install:")
    for m in models:
        labels=warning_labels(m)
        print(f"  - {m['id']}: {m['display_name']}"+(f" [{' | '.join(labels)}]" if labels else ""))
        if m.get("warning_text"): print(f"    warning: {m['warning_text']}")
        print(f"    license: {m.get('license_summary','unknown')}")

def cmd_install(args):
    data=load_manifest(args.manifest); paths=flux_paths(); models=[m for m in sorted_models(data) if m.get("default_installable") and m.get("default_enabled")] if args.all_default else [find_model(data,args.model)]
    if sys.stdin.isatty() and sys.stdout.isatty() and not args.yes and not args.dry_run:
        describe_install_selection(models)
        if not prompt_yes_no("Install these Flux AI models now?", True):
            print("AI model installation skipped by user.")
            return 0
    rc=0
    for m in models:
        r=install_one(m,args,paths)
        if r:
            rc=r
    return rc

def remove_model_id(manifest:Path, model_id:str):
    m=find_model(load_manifest(manifest),model_id); root=flux_paths()["model_store"].resolve(); target=(root/m["id"]).resolve()
    if root not in target.parents: raise ValueError("unsafe remove path refused")
    if target.exists(): shutil.rmtree(target); print(f"Removed {m['id']}: {target}")
    else: print(f"Not installed: {m['id']}")
    return 0

def cmd_remove(args):
    return remove_model_id(args.manifest,args.model)
def cmd_login(args):
    tok=os.environ.get("FLUX_HF_TOKEN") or (sys.stdin.read().strip() if args.token_stdin else getpass.getpass("Hugging Face token (hidden): "))
    if not args.remember: print("Token accepted for this command only; nothing stored."); return 0
    kr=secure_keyring()
    if not kr:
        print("No accepted secure keyring backend is available; refusing plaintext persistence. Token was not stored.")
        return 0
    kr.set_password(SERVICE,TOKEN_USER,tok); print("Hugging Face token stored in secure keyring."); return 0

def cmd_logout(args):
    kr=secure_keyring()
    if not kr: print("No accepted secure keyring backend available; no Flux token removed."); return 0
    try: kr.delete_password(SERVICE,TOKEN_USER); print("Removed Flux Hugging Face token from secure keyring.")
    except Exception: print("No Flux Hugging Face token found in secure keyring.")
    return 0

def normalize_tty_key(data:bytes)->str:
    if data in (b"\r", b"\n"): return "ENTER"
    if data == b" ": return "SPACE"
    if data in (b"q", b"Q"): return "QUIT"
    if data in (b"k", b"K"): return "UP"
    if data in (b"j", b"J"): return "DOWN"
    if data.startswith(b"\x1b[") or data.startswith(b"\x1bO"):
        final=data[-1:]
        if final == b"A": return "UP"
        if final == b"B": return "DOWN"
        if final == b"C": return "RIGHT"
        if final == b"D": return "LEFT"
    return data.decode(errors="ignore")

def read_tty_key():
    fd=sys.stdin.fileno()
    data=os.read(fd,1)
    if not data: return ""
    if data==b"\x1b":
        while select.select([fd],[],[],0.25)[0]:
            data+=os.read(fd,1)
            if len(data)>=2 and data[1:2] not in (b"[", b"O"):
                break
            if len(data)>=3 and data[-1:] in b"~ABCDHFPQRS":
                break
            if len(data)>=16:
                break
    return normalize_tty_key(data)

def draw_arrow_menu(title, rows, help_text, selected, checked, multi):
    print("\033[?25l\033[H",end="")
    print("\033[K"+title)
    print("\033[K"+help_text)
    print("\033[K")
    for i,row in enumerate(rows):
        marker="➜" if i==selected else " "
        box=("☑" if i in checked else "☐") if multi else " "
        text=f" {marker} {box} {row['title']} — {row['help']}"
        if i==selected: print("\033[K\033[7m"+text+"\033[0m")
        else: print("\033[K"+text)
    print("\033[J",end="")

def run_arrow_menu(title, rows, help_text, multi=False):
    if not rows: return [] if multi else None
    selected=0; checked={i for i,r in enumerate(rows) if r.get("checked")}; dirty=True
    old=termios.tcgetattr(sys.stdin.fileno())
    try:
        tty.setcbreak(sys.stdin.fileno())
        while True:
            if dirty:
                draw_arrow_menu(title, rows, help_text, selected, checked, multi)
                dirty=False
            key=read_tty_key()
            if key == "QUIT": return [] if multi else None
            if key == "UP":
                new_selected=max(0,selected-1)
                dirty = new_selected != selected
                selected = new_selected
            elif key == "DOWN":
                new_selected=min(len(rows)-1,selected+1)
                dirty = new_selected != selected
                selected = new_selected
            elif multi and key=="SPACE":
                if selected in checked: checked.remove(selected)
                else: checked.add(selected)
                dirty=True
            elif key=="ENTER":
                return [rows[i]["value"] for i in range(len(rows)) if i in checked] if multi else rows[selected]["value"]
    finally:
        termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old)
        print("\033[?25h\033[0m",end="")

def model_row(m, prefix=""):
    state="default" if m.get("default_installable") and m.get("default_enabled") else "optional"
    labels=warning_labels(m)
    tag=(f" [{', '.join(labels)}]" if labels else "")
    return {"title":f"{prefix}{m['id']} — {m['display_name']} ({state}){tag}","help":f"{m.get('role','model')}; {m.get('license_summary','license unknown')}","value":m}

def choose_models_interactively(data):
    models=sorted_models(data)
    defaults=[m for m in models if m.get("default_installable") and m.get("default_enabled")]
    rows=[model_row(m) for m in models]
    for i,row in enumerate(rows): row["checked"]=models[i] in defaults
    return run_arrow_menu("Available Flux AI models",rows,"Use Up/Down to move, Space to toggle models, Enter to continue, q to cancel.",multi=True)

def interactive_install(args):
    data=load_manifest(args.manifest); paths=flux_paths(); models=choose_models_interactively(data)
    if not models:
        print("No models selected."); return 0
    describe_install_selection(models)
    if not prompt_yes_no("Install selected Flux AI models now?", True):
        print("AI model installation skipped by user."); return 0
    rc=0
    for m in models:
        r=install_one(m,args,paths)
        if r==1: rc=1
    return rc

def interactive_remove(manifest:Path):
    data=load_manifest(manifest); paths=flux_paths(); installed=[m for m in sorted_models(data, include_unimplemented=True) if is_installed(m,paths)]
    if not installed:
        print("No Flux AI models are installed."); return 0
    rows=[model_row(m) for m in installed]
    m=run_arrow_menu("Installed Flux AI models",rows,"Use Up/Down to choose a model to remove, Enter to continue, q to cancel.")
    if m is None: print("Remove cancelled."); return 0
    if not prompt_yes_no(f"Remove installed model {m['id']}?", False): print("Remove cancelled."); return 0
    return remove_model_id(manifest,m["id"])

def cmd_interactive(args):
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print("ERROR: interactive model manager requires a terminal.",file=sys.stderr); return 2
    rows=[
        {"title":"Install/download AI models","help":"Choose default or optional models with an arrow-key checklist.","value":"install"},
        {"title":"Enter/remember Hugging Face token securely","help":"Hidden prompt; stores only in an accepted secure desktop keyring.","value":"login"},
        {"title":"Show AI model status","help":"Print installed/missing models and XDG cache/config/model paths.","value":"status"},
        {"title":"Remove installed AI model","help":"Choose one installed model to delete from the local model store.","value":"remove"},
        {"title":"Quit","help":"Exit without making changes.","value":"quit"},
    ]
    while True:
        choice=run_arrow_menu("Flux AI model manager",rows,"Use Up/Down to choose an action, Enter to run, q to quit.")
        if choice in {None,"quit"}: return 0
        if choice=="install": interactive_install(args)
        elif choice=="login":
            print()
            cmd_login(type("Args",(),{"remember":True,"token_stdin":False})())
        elif choice=="status": cmd_status(args)
        elif choice=="remove": interactive_remove(args.manifest)
        if choice in {"install","login","status","remove"}:
            input("\nPress Enter to return to the Flux AI model manager.")

def build_parser():
    p=argparse.ArgumentParser(description="Flux AI model manager"); p.add_argument("--manifest",type=Path,default=default_manifest_path()); sp=p.add_subparsers(dest="command",required=True)
    li=sp.add_parser("list"); li.add_argument("--json",action="store_true"); li.add_argument("--include-unimplemented",action="store_true"); li.set_defaults(func=cmd_list)
    v=sp.add_parser("verify"); v.add_argument("--offline",action="store_true"); v.set_defaults(func=cmd_verify)
    sp.add_parser("status").set_defaults(func=cmd_status)
    ins=sp.add_parser("install"); ins.add_argument("model",nargs="?"); ins.add_argument("--all-default",action="store_true"); ins.add_argument("--yes",action="store_true"); ins.add_argument("--token-stdin",action="store_true"); ins.add_argument("--dry-run",action="store_true"); ins.add_argument("--force",action="store_true"); ins.set_defaults(func=cmd_install)
    rem=sp.add_parser("remove"); rem.add_argument("model"); rem.set_defaults(func=cmd_remove)
    remi=sp.add_parser("remove-interactive"); remi.set_defaults(func=lambda args: interactive_remove(args.manifest))
    inter=sp.add_parser("interactive"); inter.add_argument("--yes",action="store_true"); inter.add_argument("--token-stdin",action="store_true"); inter.add_argument("--dry-run",action="store_true"); inter.add_argument("--force",action="store_true"); inter.set_defaults(func=cmd_interactive)
    lo=sp.add_parser("login"); lo.add_argument("--remember",action="store_true"); lo.add_argument("--token-stdin",action="store_true"); lo.set_defaults(func=cmd_login); sp.add_parser("logout").set_defaults(func=cmd_logout); return p

def main(argv=None):
    if argv is None:
        argv=sys.argv[1:]
    if not argv:
        argv=["interactive"]
    args=build_parser().parse_args(argv)
    if getattr(args,"command",None)=="install" and not args.all_default and not args.model: print("ERROR: install requires MODEL or --all-default",file=sys.stderr); return 2
    try: return int(args.func(args))
    except (OSError,json.JSONDecodeError,ValueError,KeyboardInterrupt) as e: print(f"ERROR: {e}",file=sys.stderr); return 1
if __name__=="__main__": raise SystemExit(main())
