#!/usr/bin/env python3
"""Flux external provider runtime manager: one venv per AI provider."""
from __future__ import annotations
import argparse, json, os, platform, shutil, subprocess, sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

APP_DIR="Flux"; META_NAME="flux_runtime_install.json"; SCHEMA="org.flux.ai.provider-runtime-install.v1"
HERE=Path(__file__).resolve().parent; ROOT=HERE.parent.parent
if str(HERE) not in sys.path: sys.path.insert(0,str(HERE))

def xdg_home(env:str,suffix:str)->Path: return Path(os.environ.get(env) or (Path.home()/suffix)).expanduser()
def runtime_root()->Path: return xdg_home("XDG_DATA_HOME",".local/share")/APP_DIR/"ai-envs"
def default_manifest_path()->Path: return HERE/"provider_runtime_manifest.json"
def load_manifest(path:Path)->dict[str,Any]:
    data=json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema")!="org.flux.ai.provider-runtime-manifest.v1": raise ValueError("unsupported provider runtime manifest schema")
    return data
def runtime(data:dict[str,Any], rid:str)->dict[str,Any]:
    for r in data.get("runtimes",[]):
        if r.get("id")==rid: return r
    raise ValueError(f"unknown runtime: {rid}")
def env_dir(r:dict[str,Any])->Path: return runtime_root()/r["env_dir_name"]
def venv_dir(r:dict[str,Any])->Path: return env_dir(r)/r.get("launch",{}).get("venv_dir","venv")
def env_python(r:dict[str,Any])->Path: return venv_dir(r)/"bin"/"python"
def meta_path(r:dict[str,Any])->Path: return env_dir(r)/META_NAME
def now()->str: return datetime.now(timezone.utc).isoformat()
def jprint(x:Any)->None: print(json.dumps(x,indent=2,sort_keys=True))

def py_ver(py:Path)->tuple[int,int,int]:
    out=subprocess.check_output([str(py),"-c","import sys,json;print(json.dumps(list(sys.version_info[:3])))"],text=True)
    a=json.loads(out); return int(a[0]),int(a[1]),int(a[2])
def compatible(ver:tuple[int,int,int], r:dict[str,Any])->bool:
    return (3,10,0) <= ver < (3,14,0)
def select_base(args,r):
    p=Path(args.python or os.environ.get("FLUX_AI_PYTHON") or sys.executable)
    if not p.exists(): raise ValueError(f"Python interpreter not found: {p}")
    ver=py_ver(p)
    if not compatible(ver,r): raise ValueError(f"Python {ver[0]}.{ver[1]} is not supported by current PyTorch wheels for {r['id']}; use --python /path/to/python3.12 or FLUX_AI_PYTHON.")
    return p,ver

def model_status(r):
    try:
        from flux_model_manager import default_manifest_path as mpath, find_model, flux_paths, is_installed, load_manifest, model_local_path
        data=load_manifest(mpath()); paths=flux_paths(); rows=[]
        for mid in r.get("models",[]):
            m=find_model(data,mid); lp=model_local_path(m,paths); rows.append({"id":mid,"local_path":str(lp),"installed":is_installed(m,paths)})
        return rows
    except Exception as e: return [{"error":f"{type(e).__name__}: {e}"}]

def package_import_status(r, ep:Path)->dict[str,Any]:
    packages=[p for p in r.get("packages",[]) if not p.startswith("--")]
    module_names=[]
    for pkg in packages:
        name=pkg.split(" @ ",1)[0].split("==",1)[0].split(">=",1)[0].split("<=",1)[0].split("[",1)[0]
        module_names.append({"opencv-python-headless":"cv2","huggingface-hub":"huggingface_hub","pillow":"PIL"}.get(name,name.replace("-","_")))
    module_names=sorted(set(module_names))
    if not ep.exists(): return {"checked":False,"blockers":[f"provider env python missing: {ep}"],"modules":{}}
    code="""import importlib, json\nmods=%r\nout={}\nfor m in mods:\n    try:\n        mod=importlib.import_module(m); out[m]={\"available\":True,\"version\":getattr(mod,\"__version__\",None)}\n    except Exception as e:\n        out[m]={\"available\":False,\"error\":type(e).__name__+\": \"+str(e)}\nprint(json.dumps(out))\n""" % module_names
    try:
        p=subprocess.run([str(ep),"-c",code],text=True,capture_output=True,timeout=60)
        modules=json.loads(p.stdout) if p.stdout else {}
        blockers=[f"missing runtime module: {m} ({v.get('error')})" for m,v in modules.items() if not v.get("available")]
        if p.returncode != 0: blockers.append(f"runtime import check failed with rc {p.returncode}: {p.stderr.strip()}")
        return {"checked":True,"blockers":blockers,"modules":modules}
    except Exception as e:
        return {"checked":False,"blockers":[f"runtime import check failed: {type(e).__name__}: {e}"],"modules":{}}

def status_payload(r):
    ep=env_python(r); meta=None
    if meta_path(r).is_file():
        try: meta=json.loads(meta_path(r).read_text(encoding="utf-8"))
        except Exception as e: meta={"parse_error":str(e)}
    blockers=[]
    if not ep.exists(): blockers.append("provider venv python is missing")
    if not meta: blockers.append("runtime install metadata is missing")
    models=model_status(r)
    for m in models:
        if m.get("installed") is False: blockers.append(f"model asset missing: {m['id']}")
    imports=package_import_status(r, ep)
    blockers.extend(imports.get("blockers",[]))
    return {"schema":"org.flux.ai.provider-runtime-status.v1","id":r["id"],"runtime_root":str(runtime_root()),"env_dir":str(env_dir(r)),"venv_dir":str(venv_dir(r)),"python":str(ep),"python_exists":ep.exists(),"metadata":meta,"models":models,"runtime_imports":imports,"ready":not blockers,"blockers":blockers}

def cmd_list(args):
    data=load_manifest(args.manifest); rows=[{"id":r["id"],"display_name":r.get("display_name"),"env_dir":str(env_dir(r)),"models":r.get("models",[])} for r in data.get("runtimes",[])]
    jprint({"runtimes":rows}) if args.json else [print(f"{x['id']}: {x['display_name']} -> {x['env_dir']}") for x in rows]; return 0

def cmd_status(args):
    r=runtime(load_manifest(args.manifest),args.runtime); p=status_payload(r); jprint(p) if args.json else print(f"{p['id']}: {'ready' if p['ready'] else 'blocked'} ({p['env_dir']})"); return 0

def cmd_python(args):
    print(env_python(runtime(load_manifest(args.manifest),args.runtime))); return 0

def cmd_install(args):
    r=runtime(load_manifest(args.manifest),args.runtime); indices=r.get("cuda",{}).get("indices",{})
    if args.cuda not in indices: raise ValueError(f"unsupported CUDA selector {args.cuda}; choose one of: {', '.join(sorted(indices))}")
    base,ver=select_base(args,r); ed=env_dir(r); vd=venv_dir(r); ed.mkdir(parents=True,exist_ok=True)
    if vd.exists():
        existing=env_python(r)
        if not existing.exists() or py_ver(existing)[:2] != ver[:2]:
            shutil.rmtree(vd)
    subprocess.check_call([str(base),"-m","venv",str(vd)])
    ep=env_python(r)
    subprocess.check_call([str(ep),"-m","pip","install","--upgrade","pip","setuptools","wheel"])
    subprocess.check_call([str(ep),"-m","pip","install","--index-url",indices[args.cuda],"torch","torchvision"])
    rest=[p for p in r.get("packages",[]) if p not in {"torch","torchvision"}]
    if rest: subprocess.check_call([str(ep),"-m","pip","install",*rest])
    meta={"schema":SCHEMA,"provider_id":r["id"],"env_dir":str(ed),"venv_dir":str(vd),"python_executable":str(ep),"base_python":str(base),"requested_python_version":"%d.%d.%d"%ver,"packages":r.get("packages",[]),"torch_cuda_selector":args.cuda,"torch_index_url":indices[args.cuda],"installed_at":now(),"self_check_summary":None,"notes":"model weights are not downloaded by runtime install"}
    meta_path(r).write_text(json.dumps(meta,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    jprint(meta) if args.json else print(f"Installed {r['id']} runtime -> {ed}"); return 0

def cmd_self(args):
    r=runtime(load_manifest(args.manifest),args.runtime); ep=env_python(r)
    if not ep.exists():
        payload={"schema":"org.flux.ai.provider-runtime-self-check.v1","id":r["id"],"ready":False,"blockers":[f"provider env python missing: {ep}"],"python":str(ep)}; jprint(payload); return 2
    sc=r["self_check"]; cmd=[str(ep),str(ROOT/sc["script"]),*sc.get("args",[])]
    p=subprocess.run(cmd,cwd=str(ROOT),text=True,capture_output=True)
    try: payload=json.loads(p.stdout)
    except Exception: payload={"ready":False,"blockers":["self-check did not emit JSON"],"stdout":p.stdout,"stderr":p.stderr}
    summary={"checked_at":now(),"returncode":p.returncode,"ready":bool(payload.get("ready")),"blockers":payload.get("blockers",[])}
    if meta_path(r).is_file():
        meta=json.loads(meta_path(r).read_text(encoding="utf-8")); meta["self_check_summary"]=summary; meta_path(r).write_text(json.dumps(meta,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    jprint(payload if args.json else summary); return p.returncode

def parser():
    p=argparse.ArgumentParser(description="Flux provider runtime manager"); p.add_argument("--manifest",type=Path,default=default_manifest_path()); sp=p.add_subparsers(dest="command",required=True)
    li=sp.add_parser("list"); li.add_argument("--json",action="store_true"); li.set_defaults(func=cmd_list)
    st=sp.add_parser("status"); st.add_argument("runtime"); st.add_argument("--json",action="store_true"); st.set_defaults(func=cmd_status)
    py=sp.add_parser("python"); py.add_argument("runtime"); py.set_defaults(func=cmd_python)
    ins=sp.add_parser("install"); ins.add_argument("runtime"); ins.add_argument("--python"); ins.add_argument("--cuda",default="cu128"); ins.add_argument("--json",action="store_true"); ins.set_defaults(func=cmd_install)
    sc=sp.add_parser("self-check"); sc.add_argument("runtime"); sc.add_argument("--json",action="store_true"); sc.set_defaults(func=cmd_self); return p

def main(argv=None):
    args=parser().parse_args(argv)
    try: return int(args.func(args))
    except (OSError,ValueError,subprocess.CalledProcessError,json.JSONDecodeError) as e: print(f"ERROR: {e}",file=sys.stderr); return 1
if __name__=="__main__": raise SystemExit(main())
