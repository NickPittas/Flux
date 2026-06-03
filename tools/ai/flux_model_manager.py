#!/usr/bin/env python3
"""Flux AI model manager: install/verify/remove/list/status with secure HF token use."""
from __future__ import annotations
import argparse, getpass, json, os, select, shutil, sys, tempfile, termios, tty, urllib.request
from pathlib import Path
from typing import Any, Dict, Iterable, List

APP_DIR="Flux"; SERVICE="org.flux.Flux"; TOKEN_USER="huggingface.token"
REQUIRED_TOP_LEVEL={"manifest_version","models"}
REQUIRED_MODEL_FIELDS={"id","display_name","category","role","priority","provider","runtime_type","install_policy","bundling_policy","license_summary","warning_text","gated_token_requirement","default_enabled","default_installable","output_use_notes","source"}

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

def sorted_models(data): return sorted(data.get("models",[]), key=lambda m:(-int(m.get("priority",0)), str(m.get("id",""))))
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
    if getattr(args,"json",False):
        paths=flux_paths()
        rows=[]
        for m in sorted_models(data):
            rows.append({"id":m["id"],"display_name":m.get("display_name",""),"role":m.get("role",""),"license_summary":m.get("license_summary",""),"warning_text":m.get("warning_text",""),"default_enabled":bool(m.get("default_enabled")),"default_installable":bool(m.get("default_installable")),"installed":is_installed(m,paths),"labels":warning_labels(m)})
        print(json.dumps({"models":rows},sort_keys=True))
        return 0
    for m in sorted_models(data):
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
    for m in sorted_models(data):
        if is_installed(m,paths): print(f"OK installed: {m['id']} -> {model_local_path(m,paths)}")
    return bad

def cmd_status(args):
    data=load_manifest(args.manifest); paths=flux_paths(); print("Flux AI model paths:")
    for k in ("model_store","hub_cache","download_staging","config_file"): print(f"  {k}: {paths[k]} (exists: {paths[k].exists()})")
    for m in sorted_models(data): print(f"  {m['id']}: {'installed' if is_installed(m,paths) else 'missing'} ({model_local_path(m,paths)})")
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
    tok=os.environ.get("FLUX_HF_TOKEN")
    if tok: return tok
    if getattr(args,"token_stdin",False): return sys.stdin.read().strip()
    tok=keyring_token()
    if tok: return tok
    if needed and sys.stdin.isatty() and not getattr(args,"yes",False):
        tok=getpass.getpass("Hugging Face token (input hidden): ").strip()
        if tok: remember_token_securely(tok)
        return tok or None
    return None

def write_meta(m,path,files):
    meta={"model_id":m["id"],"display_name":m.get("display_name"),"source":m.get("source"),"files":files,"warnings":m.get("warning_text"),"license_summary":m.get("license_summary")}
    (path/"flux_model_install.json").write_text(json.dumps(meta,indent=2,sort_keys=True),encoding="utf-8")

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
    dest=model_local_path(m,paths); dest.parent.mkdir(parents=True,exist_ok=True); paths["hub_cache"].mkdir(parents=True,exist_ok=True)
    snapshot_download(repo_id=s["repo"], revision=s.get("revision"), token=tok, cache_dir=str(paths["hub_cache"]), local_dir=str(dest), allow_patterns=s.get("allow_patterns"))
    files=[str(p.relative_to(dest)) for p in dest.rglob("*") if p.is_file() and p.name!="flux_model_install.json"]; write_meta(m,dest,files); print(f"Installed {m['id']} -> {dest}"); return 0

def install_direct(m,args,paths):
    s=m["source"]; urls=s.get("urls") or ([s.get("url")] if s.get("url") else [])
    if args.dry_run: print(f"DRY-RUN URL {m['id']}: "+", ".join(u.get('url',u) if isinstance(u,dict) else u for u in urls)); return 0
    dest=model_local_path(m,paths); dest.mkdir(parents=True,exist_ok=True); files=[]
    for item in urls:
        url=item.get("url") if isinstance(item,dict) else item; name=item.get("filename") if isinstance(item,dict) else Path(url).name
        print(f"Downloading {m['id']} file {name}"); urllib.request.urlretrieve(url, dest/name); files.append(name)
    write_meta(m,dest,files); print(f"Installed {m['id']} -> {dest}"); return 0

def install_one(m,args,paths):
    if not m.get("default_installable",False) and not args.force: print(f"SKIP {m['id']}: not installable by default (use --force).",file=sys.stderr); return 1
    if m.get("warning_text") and not (args.yes or args.dry_run):
        print("WARNING: "+m["warning_text"]); input("Press Enter to continue or Ctrl-C to abort.")
    typ=m.get("source",{}).get("type")
    if typ=="huggingface": return install_hf(m,args,paths)
    if typ=="url": return install_direct(m,args,paths)
    print(f"ERROR: {m['id']} has unsupported source type {typ}",file=sys.stderr); return 1

def describe_install_selection(models):
    print("Flux AI model setup will install:")
    for m in models:
        labels=warning_labels(m)
        print(f"  - {m['id']}: {m['display_name']}"+(f" [{', '.join(labels)}]" if labels else ""))
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
        if r==1: rc=1
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
    data=load_manifest(manifest); paths=flux_paths(); installed=[m for m in sorted_models(data) if is_installed(m,paths)]
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
    li=sp.add_parser("list"); li.add_argument("--json",action="store_true"); li.set_defaults(func=cmd_list); v=sp.add_parser("verify"); v.add_argument("--offline",action="store_true"); v.set_defaults(func=cmd_verify); sp.add_parser("status").set_defaults(func=cmd_status)
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
