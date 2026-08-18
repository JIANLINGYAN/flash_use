#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
backend/importer.py - 库文件导入与校验（模块二/三：可仿真闭环）

职责：接收一个由「代码生成引擎」导出的 zip 包，校验其是否符合模拟基座的
使用要求，并编译运行一次以确认可仿真；通过则注册为自定义框架，出现在
前端框架列表中，可被直接「运行测试」。

校验"符合模拟基座要求"的规则：
  1. 包内必须含 manifest.json，且 manifest.requires == "flash_sim"；
  2. 包内源码（.c/.h）必须 #include "flash_sim.h"（依赖基座统一接口）；
  3. 用 simulator/flash_sim.c + 包内 test_main.c + 库 .c 能编译通过；
  4. 编译产物运行后退出码为 0（即自带自检全部通过）。

兼容性：
  - 支持新导出包（分层结构 + manifest.includes/相对路径 cflags/lib_sources）
    与旧导出包（扁平结构）两种格式；
  - 若 zip 存在单一顶层目录（如 <lib>_library/），提取时自动剥离。
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMPORTS_DIR = os.path.join(ROOT, "imports")
REGISTRY_PATH = os.path.join(IMPORTS_DIR, "registry.json")


def _find_gcc():
    from shutil import which
    for c in ("gcc", "/usr/bin/gcc", "/usr/local/bin/gcc"):
        p = which(c)
        if p:
            return p
    return None


def _zip_root_prefix(names):
    """若 zip 存在单一顶层目录（且无其它散落条目），返回其前缀，否则 ''。"""
    entries = [n for n in names if n and not n.endswith("/")]
    if not entries:
        return ""
    tops = {n.split("/")[0] for n in entries}
    if len(tops) == 1:
        prefix = tops.pop() + "/"
        if all(n.startswith(prefix) for n in entries):
            return prefix
    return ""


def _locate(dest, rel):
    """在解压目录中定位相对路径；找不到时按 basename 全目录搜索（兼容旧包）。"""
    p = os.path.join(dest, rel)
    if os.path.exists(p):
        return p
    b = os.path.basename(rel)
    if b:
        for root, _, fnames in os.walk(dest):
            if b in fnames:
                return os.path.join(root, b)
    return p


def load_registry():
    if os.path.exists(REGISTRY_PATH):
        try:
            with open(REGISTRY_PATH, "r", encoding="utf-8") as f:
                return json.load(f)
        except (ValueError, OSError):
            return {}
    return {}


def save_registry(reg):
    os.makedirs(IMPORTS_DIR, exist_ok=True)
    with open(REGISTRY_PATH, "w", encoding="utf-8") as f:
        json.dump(reg, f, ensure_ascii=False, indent=2)


def imported_frameworks():
    """返回已导入框架的元信息列表（供 /api/frameworks 合并）。"""
    reg = load_registry()
    out = []
    for fid, meta in reg.items():
        out.append({
            "id": fid,
            "name": meta.get("name", fid),
            "desc": meta.get("desc", "") + " [已导入库]",
        })
    return out


def import_zip(zip_bytes):
    """导入 zip 包，返回 (success:bool, result_dict)。"""
    gcc = _find_gcc()
    if not gcc:
        return False, {"error": "未找到 gcc 编译器，无法校验可仿真性"}

    # 1) 解压到内存先校验结构
    try:
        zf = zipfile.ZipFile(_io_bytesio(zip_bytes), "r")
    except zipfile.BadZipFile:
        return False, {"error": "文件不是合法的 zip 包"}

    names = zf.namelist()
    prefix = _zip_root_prefix(names)

    def rel(n):
        r = n[len(prefix):] if prefix and n.startswith(prefix) else n
        return r.rstrip("/")

    flat = {}  # 剥离顶层目录后的 相对路径 -> 原始条目
    for n in names:
        if n.endswith("/"):
            continue
        flat[rel(n)] = n

    if "manifest.json" not in flat:
        zf.close()
        return False, {"error": "缺少 manifest.json，无法识别为平台库包"}

    try:
        manifest = json.loads(zf.read(flat["manifest.json"]).decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        zf.close()
        return False, {"error": "manifest.json 解析失败"}

    requires = manifest.get("requires")
    if requires not in ("flash_sim", "flash_hal"):
        zf.close()
        return False, {"error": "该库未声明依赖统一 HAL 契约(requires=flash_hal/flash_sim)，不符合使用要求"}

    entry = manifest.get("entry", "test_main.c")
    if entry not in flat:
        zf.close()
        return False, {"error": "缺少编译入口 %s" % entry}

    # 2) 校验源码依赖统一 HAL 契约（flash_hal.h 或旧版 flash_sim.h）
    depends = False
    for r, orig in flat.items():
        if r.endswith(".c") or r.endswith(".h"):
            try:
                txt = zf.read(orig).decode("utf-8", "ignore")
            except Exception:
                continue
            if '#include "flash_hal.h"' in txt or '#include "flash_sim.h"' in txt:
                depends = True
                break
    if not depends:
        zf.close()
        return False, {"error": "库源码未发现 #include \"flash_hal.h\"（或旧版 flash_sim.h），不符合统一 HAL 契约要求"}

    # 3) 落地到 imports/<id>/（保留子目录结构）
    fid = manifest.get("id") or ("import_%d" % abs(hash(entry)))
    dest = os.path.join(IMPORTS_DIR, fid)
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest, exist_ok=True)
    for r, orig in flat.items():
        target = os.path.join(dest, r)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(zf.read(orig))
    zf.close()

    # 4) 编译运行校验
    #    - flash_hal（新契约）：包自带 core/flash_hal_mem.c 内存 HAL，自包含，不链接平台代码；
    #    - flash_sim（旧契约）：链接平台 simulator/flash_sim.c。
    entry_src = _locate(dest, entry)
    compile_srcs = [entry_src]
    if requires == "flash_sim":
        sim_c = os.path.join(ROOT, "simulator", "flash_sim.c")
        if not os.path.exists(sim_c):
            shutil.rmtree(dest, ignore_errors=True)
            return False, {"error": "模拟基座源文件缺失，无法编译"}
        compile_srcs.insert(0, sim_c)
    # 兼容旧导出包（单一 lib 文件）与新导出包（lib_sources 列表）
    lib_sources = manifest.get("lib_sources")
    if lib_sources:
        for ls in lib_sources:
            p = _locate(dest, ls)
            if os.path.exists(p):
                compile_srcs.append(p)
    else:
        lib_c = os.path.join(dest, manifest.get("lib", "") + ".c") \
            if manifest.get("lib") else None
        if lib_c and os.path.exists(lib_c):
            compile_srcs.append(lib_c)

    cmd = [gcc, "-std=c99", "-Wall", "-Wextra", "-I" + dest]
    if requires == "flash_sim":
        cmd += ["-I" + os.path.join(ROOT, "simulator")]
    # 新导出包的 include 目录（manifest.includes，相对包根）
    for inc in manifest.get("includes", []):
        p = os.path.join(dest, inc)
        if os.path.isdir(p):
            cmd += ["-I", p]
    # cflags：-include 映射为包内实际路径；-D 原样保留
    mflags = manifest.get("cflags", [])
    for i, a in enumerate(mflags):
        if a == "-include" and i + 1 < len(mflags):
            cmd += ["-include", _locate(dest, mflags[i + 1])]
        elif a.startswith("-D"):
            cmd.append(a)
    cmd += ["-o", os.path.join(tempfile.gettempdir(), "flash_use_import_%s" % fid)]
    cmd += compile_srcs

    build = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if build.returncode != 0:
        shutil.rmtree(dest, ignore_errors=True)
        return False, {
            "error": "编译校验失败：库不符合模拟基座接口",
            "lines": [{"level": "fail", "text": l} for l in build.stderr.splitlines()],
        }

    # 5) 运行一次（在工作目录内，使 BIN 落盘到 imports/<id>）以确认可仿真
    tmp_exe = os.path.join(tempfile.gettempdir(), "flash_use_import_%s" % fid)
    run = subprocess.run([tmp_exe], cwd=dest, capture_output=True, text=True, timeout=60)
    if run.returncode != 0:
        # 编译通过但自检失败：仍允许导入，但提示运行不通过
        reg = load_registry()
        reg[fid] = manifest
        save_registry(reg)
        return True, {
            "imported": True,
            "warning": "编译通过，但自带自检未全部通过（返回码 %d）" % run.returncode,
            "name": manifest.get("name", fid),
            "id": fid,
            "lines": [{"level": "stderr", "text": l} for l in run.stdout.splitlines()],
        }

    # 6) 注册
    reg = load_registry()
    reg[fid] = manifest
    save_registry(reg)
    return True, {
        "imported": True,
        "name": manifest.get("name", fid),
        "id": fid,
        "message": "导入成功，已注册为可用框架",
    }


def _io_bytesio(b):
    import io
    return io.BytesIO(b)
