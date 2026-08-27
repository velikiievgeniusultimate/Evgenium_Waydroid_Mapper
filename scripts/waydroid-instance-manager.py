#!/usr/bin/env python3
from __future__ import annotations

import argparse, configparser, datetime as dt, json, os, pathlib, platform, shutil, subprocess, sys, tempfile, urllib.request
from typing import Any

SYSTEM = "https://ota.waydro.id/system"
VENDOR = "https://ota.waydro.id/vendor"
STORE = pathlib.Path("/var/lib/ewm-waydroid")
REGISTRY = STORE / "registry.json"
LIVE = pathlib.Path("/var/lib/waydroid")
SCHEMA = 1
ANDROID = {"18.1": "11", "20": "13", "20.0": "13"}
REQUIRED = ("waydroid.cfg", "waydroid_base.prop", "images/system.img", "images/vendor.img", "lxc/waydroid/config")

class Error(RuntimeError): pass

def log(s: str): print(f"[EWM-INSTANCE] {s}", flush=True)
def dump(v: Any): print(json.dumps(v, ensure_ascii=False, separators=(",", ":")), flush=True)
def fetch(url: str):
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent":"EWM-Waydroid-Manager/1"}), timeout=30) as r:
            return json.loads(r.read().decode())
    except Exception as e: raise Error(f"Не удалось получить {url}: {e}") from e

def arch():
    m=platform.machine().lower()
    if m in ("x86_64","amd64"): return "x86_64"
    if m in ("aarch64","arm64"): return "arm64"
    raise Error(f"Неподдерживаемая архитектура: {m}")

def android(lineage: str): return ANDROID.get(lineage, ANDROID.get(lineage.split('.')[0], f"Lineage {lineage}"))
def date(ts: int):
    try: return dt.datetime.fromtimestamp(ts, dt.timezone.utc).strftime("%Y%m%d")
    except Exception: return str(ts)
def sys_url(variant: str): return f"{SYSTEM}/lineage/waydroid_{arch()}/{variant}.json"
def ven_url(): return f"{VENDOR}/waydroid_{arch()}/MAINLINE.json"

def catalog(variant: str):
    variant=variant.upper(); systems=fetch(sys_url(variant)).get("response",[]); vendors=fetch(ven_url()).get("response",[])
    latest={}
    for x in systems:
        v=str(x.get("version","")); old=latest.get(v)
        if v and (old is None or int(x.get("datetime",0))>int(old.get("datetime",0))): latest[v]=x
    out=[]
    for v,s in latest.items():
        vs=[x for x in vendors if str(x.get("version",""))==v]
        if not vs: continue
        n=max(vs,key=lambda x:int(x.get("datetime",0)))
        out.append({"android":android(v),"lineage":v,"variant":variant,"build":date(int(s.get("datetime",0))),
                    "system_datetime":int(s.get("datetime",0)),"vendor_datetime":int(n.get("datetime",0)),
                    "system_size":int(s.get("size",0)),"vendor_size":int(n.get("size",0)),
                    "total_size":int(s.get("size",0))+int(n.get("size",0)),"system":s,"vendor":n})
    out.sort(key=lambda x:int(x["android"]) if str(x["android"]).isdigit() else -1, reverse=True); return out

def chosen(a: str, variant: str):
    for x in catalog(variant):
        if x["android"]==a: return x
    raise Error(f"В официальном OTA нет Android {a} ({variant}) для {arch()}")

def initialized(root: pathlib.Path):
    try: return all((root/p).is_file() and (root/p).stat().st_size>0 for p in REQUIRED)
    except OSError: return False

def cfg(root=LIVE):
    p=configparser.ConfigParser(interpolation=None)
    try: p.read(root/"waydroid.cfg",encoding="utf-8")
    except Exception: return {}
    return dict(p.items("waydroid")) if p.has_section("waydroid") else {}

def current_meta():
    c=cfg(); ts=int(c.get("system_datetime","0") or 0); ota=c.get("system_ota","")
    variant="GAPPS" if "GAPPS" in ota.upper() else ("VANILLA" if "VANILLA" in ota.upper() else "UNKNOWN")
    lineage=""
    for vv in ([variant] if variant in ("GAPPS","VANILLA") else ["GAPPS","VANILLA"]):
        try:
            for x in fetch(sys_url(vv)).get("response",[]):
                if int(x.get("datetime",0))==ts: lineage=str(x.get("version","")); variant=vv; break
        except Error: pass
        if lineage: break
    return {"android":android(lineage) if lineage else "unknown","lineage":lineage or "unknown","variant":variant,
            "build":date(ts) if ts else "unknown","system_datetime":ts,
            "vendor_datetime":int(c.get("vendor_datetime","0") or 0),"imported":True}

def read_registry():
    if not REGISTRY.is_file(): return None
    try: r=json.loads(REGISTRY.read_text())
    except Exception as e: raise Error(f"Повреждён реестр {REGISTRY}: {e}")
    if r.get("schema")!=SCHEMA or not isinstance(r.get("instances"),dict): raise Error("Неподдерживаемый реестр EWM")
    return r

def save(r):
    STORE.mkdir(parents=True,exist_ok=True); fd,tmp=tempfile.mkstemp(prefix="registry.",dir=STORE)
    try:
        with os.fdopen(fd,"w") as f: json.dump(r,f,ensure_ascii=False,indent=2,sort_keys=True); f.write("\n"); f.flush(); os.fsync(f.fileno())
        os.chmod(tmp,0o644); os.replace(tmp,REGISTRY)
    finally:
        try: os.unlink(tmp)
        except FileNotFoundError: pass

def iid(meta, r):
    base=f"android-{meta.get('android','unknown')}-{str(meta.get('variant','unknown')).lower()}-{meta.get('build','unknown')}"
    base=''.join(c if c.isalnum() or c in '-_' else '-' for c in base); x=base; n=2
    while x in r["instances"]: x=f"{base}-{n}"; n+=1
    return x

def work(i): return STORE/"instances"/i/"work"
def user(data_home,i): return data_home/"ewm-waydroid"/"instances"/i/"user"
def live_user(data_home): return data_home/"waydroid"
def own_dir(p,uid,gid):
    p.mkdir(parents=True,exist_ok=True)
    try: os.chown(p,uid,gid)
    except PermissionError: pass

def marker(root,i,m): (root/".ewm-instance.json").write_text(json.dumps({**m,"id":i},ensure_ascii=False,indent=2)+"\n")
def move(a,b):
    if not a.exists() and not a.is_symlink(): return
    if b.exists() or b.is_symlink(): raise Error(f"Цель уже существует: {b}")
    b.parent.mkdir(parents=True,exist_ok=True); os.replace(a,b)

def adopt(data_home,uid,gid):
    r=read_registry()
    if r:
        if int(r.get("owner_uid",uid))!=uid: raise Error(f"Набор Waydroid принадлежит UID {r.get('owner_uid')}")
        return r
    if not initialized(LIVE): raise Error("Текущий /var/lib/waydroid не является полной установкой")
    m=current_meta(); r={"schema":SCHEMA,"owner_uid":uid,"owner_gid":gid,"data_home":str(data_home),"active":"","instances":{}}
    i=iid(m,r); m["id"]=i; r["instances"][i]=m; r["active"]=i
    (STORE/"instances"/i).mkdir(parents=True,exist_ok=True); own_dir(data_home/"ewm-waydroid"/"instances"/i,uid,gid); marker(LIVE,i,m); save(r)
    log(f"Текущий Waydroid принят в менеджер как {i}"); return r

def run(cmd,check=True,timeout=None):
    p=subprocess.run(cmd,text=True,timeout=timeout,check=False)
    if check and p.returncode: raise Error(f"Команда завершилась с кодом {p.returncode}: {' '.join(cmd)}")
    return p

def stop():
    log("Останавливаю Waydroid перед операцией…"); wd=shutil.which("waydroid") or "/usr/bin/waydroid"
    try: run([wd,"container","stop"],False,30)
    except Exception: pass
    if shutil.which("systemctl"):
        try: run(["systemctl","stop","waydroid-container.service"],False,30)
        except Exception: pass
    if shutil.which("lxc-stop"):
        try: run(["lxc-stop","-P",str(LIVE/"lxc"),"-n","waydroid","-k"],False,10)
        except Exception: pass
    if shutil.which("umount"):
        for p in (LIVE/"rootfs",LIVE/"data"):
            try:
                if p.exists(): run(["umount","-R",str(p)],False,15)
            except Exception: pass

def park(r,data_home):
    i=str(r.get("active","")); dest=work(i); lu=live_user(data_home); du=user(data_home,i)
    if not i or i not in r["instances"]: raise Error("Не определён активный Waydroid")
    if LIVE.is_symlink() or lu.is_symlink(): raise Error("EWM не переключает установки с symlink в canonical Waydroid paths")
    if not LIVE.exists(): raise Error("Активный /var/lib/waydroid отсутствует")
    if dest.exists() or (lu.exists() and du.exists()): raise Error("Хранилище активного инстанса уже занято")
    move(LIVE,dest)
    try:
        if lu.exists(): move(lu,du)
    except Exception:
        if dest.exists() and not LIVE.exists(): move(dest,LIVE)
        raise
    return i

def restore(r,data_home,uid,gid,i):
    src=work(i); su=user(data_home,i); lu=live_user(data_home)
    if not src.exists(): raise Error(f"Не найдены system data {i}")
    if LIVE.exists() or LIVE.is_symlink() or lu.exists() or lu.is_symlink(): raise Error("Canonical Waydroid path занят")
    move(src,LIVE)
    try:
        if su.exists(): move(su,lu)
        else: own_dir(lu,uid,gid)
    except Exception:
        if LIVE.exists() and not src.exists(): move(LIVE,src)
        raise
    r["active"]=i

def switch(data_home,uid,gid,target):
    if os.geteuid()!=0: raise Error("Переключение требует root")
    r=adopt(data_home,uid,gid)
    if target not in r["instances"]: raise Error(f"Неизвестный инстанс: {target}")
    if target==r["active"]: log("Этот Android уже активен"); return
    if not work(target).exists(): raise Error(f"Отсутствует work directory {target}")
    stop(); old=park(r,data_home)
    try: restore(r,data_home,uid,gid,target); save(r)
    except Exception:
        try:
            if LIVE.exists(): shutil.rmtree(LIVE,ignore_errors=True)
            lu=live_user(data_home)
            if lu.exists() and not lu.is_symlink(): shutil.rmtree(lu,ignore_errors=True)
            restore(r,data_home,uid,gid,old); save(r)
        except Exception as e: raise Error(f"Не удалось откатить переключение: {e}") from e
        raise
    log(f"Активирован {target}")

def local_ota(root,s,v,variant):
    a=arch(); sd=root/"ewm-ota/system/lineage"/f"waydroid_{a}"; vd=root/"ewm-ota/vendor"/f"waydroid_{a}"
    sd.mkdir(parents=True); vd.mkdir(parents=True)
    (sd/f"{variant}.json").write_text(json.dumps({"response":[s]},indent=2)+"\n"); (vd/"MAINLINE.json").write_text(json.dumps({"response":[v]},indent=2)+"\n")
    return (root/"ewm-ota/system").as_uri(),(root/"ewm-ota/vendor").as_uri()

def install(data_home,uid,gid,a,variant):
    if os.geteuid()!=0: raise Error("Установка требует root")
    if not shutil.which("waydroid"): raise Error("waydroid не найден")
    variant=variant.upper(); c=chosen(a,variant); r=adopt(data_home,uid,gid)
    for i,m in r["instances"].items():
        if int(m.get("system_datetime",0))==c["system_datetime"] and m.get("variant")==variant: log(f"Эта сборка уже установлена как {i}"); return
    m={k:c[k] for k in ("android","lineage","variant","build","system_datetime","vendor_datetime")}; m["imported"]=False; i=iid(m,r); m["id"]=i
    stop(); old=park(r,data_home); lu=live_user(data_home)
    try:
        LIVE.mkdir(); own_dir(lu,uid,gid); sc,vc=local_ota(LIVE,c["system"],c["vendor"],variant)
        log(f"Загружаю Android {a} / Lineage {c['lineage']} {variant}, build {c['build']}…")
        run([shutil.which("waydroid"),"init","-f","-c",sc,"-v",vc,"-s",variant],True)
        if not initialized(LIVE): raise Error("waydroid init не создал полную установку")
        marker(LIVE,i,m); move(LIVE,work(i));
        if lu.exists(): move(lu,user(data_home,i))
        r["instances"][i]=m; restore(r,data_home,uid,gid,old); save(r)
    except Exception:
        stop();
        if LIVE.exists(): shutil.rmtree(LIVE,ignore_errors=True)
        if lu.exists() and not lu.is_symlink(): shutil.rmtree(lu,ignore_errors=True)
        shutil.rmtree(STORE/"instances"/i,ignore_errors=True); shutil.rmtree(data_home/"ewm-waydroid/instances"/i,ignore_errors=True)
        if work(old).exists(): restore(r,data_home,uid,gid,old); save(r)
        raise
    log(f"Android {a} установлен как {i}; предыдущий Android оставлен активным")

def delete(data_home,uid,gid,i):
    if os.geteuid()!=0: raise Error("Удаление требует root")
    r=adopt(data_home,uid,gid)
    if i not in r["instances"]: raise Error(f"Неизвестный инстанс: {i}")
    if i==r["active"]: raise Error("Нельзя удалить активный Android")
    shutil.rmtree(STORE/"instances"/i,ignore_errors=True); shutil.rmtree(data_home/"ewm-waydroid/instances"/i,ignore_errors=True); del r["instances"][i]; save(r); log(f"Удалён {i}")

def status(data_home):
    r=read_registry()
    if not r:
        if initialized(LIVE): return {"ok":True,"managed":False,"active":"unmanaged-current","instances":[{**current_meta(),"id":"unmanaged-current","active":True,"installed":True}]}
        return {"ok":True,"managed":False,"active":"","instances":[]}
    out=[]
    for i,m in r["instances"].items():
        active=i==r.get("active"); out.append({**m,"id":i,"active":active,"installed":initialized(LIVE if active else work(i))})
    out.sort(key=lambda x:(not x["active"],str(x.get("android","")),str(x.get("build",""))))
    return {"ok":True,"managed":True,"active":r.get("active",""),"instances":out}

def parser():
    p=argparse.ArgumentParser(); p.add_argument("--data-home",required=True); p.add_argument("--uid",required=True,type=int); p.add_argument("--gid",required=True,type=int); s=p.add_subparsers(dest="cmd",required=True)
    s.add_parser("status"); c=s.add_parser("catalog"); c.add_argument("--variant",choices=["GAPPS","VANILLA"],default="GAPPS")
    i=s.add_parser("install"); i.add_argument("--android",required=True); i.add_argument("--variant",choices=["GAPPS","VANILLA"],default="GAPPS")
    x=s.add_parser("switch"); x.add_argument("--id",required=True); d=s.add_parser("delete"); d.add_argument("--id",required=True); return p

def main():
    a=parser().parse_args(); dh=pathlib.Path(a.data_home).expanduser().resolve()
    try:
        if a.cmd=="status": dump(status(dh))
        elif a.cmd=="catalog": dump({"ok":True,"items":catalog(a.variant)})
        elif a.cmd=="install": install(dh,a.uid,a.gid,a.android,a.variant)
        elif a.cmd=="switch": switch(dh,a.uid,a.gid,a.id)
        elif a.cmd=="delete": delete(dh,a.uid,a.gid,a.id)
        return 0
    except Exception as e:
        print(f"[EWM-INSTANCE] ERROR: {e}",file=sys.stderr,flush=True); return 1
if __name__=="__main__": raise SystemExit(main())
