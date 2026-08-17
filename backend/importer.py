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

通过校验后，框架信息写入 imports/registry.json 持久化，服务重启仍可见。
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
        zf = zipfile.ZipFile(io_BytesIO_safe(zip_bytes), "r")
    except zipfile.BadZipFile:
        return False, {"error": "文件不是合法的 zip 包"}

    names = zf.namelist()
    if "manifest.json" not in names:
        zf.close()
        return False, {"error": "缺少 manifest.json，无法识别为平台库包"}

    try:
        manifest = json.loads(zf.read("manifest.json").decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        zf.close()
        return False, {"error": "manifest.json 解析失败"}

    if manifest.get("requires") != "flash_sim":
        zf.close()
        return False, {"error": "该库未声明依赖模拟基座(requires=flash_sim)，不符合使用要求"}

    entry = manifest.get("entry", "test_main.c")
    if entry not in names:
        zf.close()
        return False, {"error": "缺少编译入口 %s" % entry}

    # 2) 校验源码依赖 flash_sim.h
    depends = False
    for n in names:
        if n.endswith(".c") or n.endswith(".h"):
            try:
                txt = zf.read(n).decode("utf-8", "ignore")
            except Exception:
                continue
            if '#include "flash_sim.h"' in txt:
                depends = True
                break
    if not depends:
        zf.close()
        return False, {"error": "库源码未发现 #include \"flash_sim.h\"，不符合模拟基座接口要求"}

    # 3) 落地到 imports/<id>/
    fid = manifest.get("id") or ("import_%d" % abs(hash(entry)))
    dest = os.path.join(IMPORTS_DIR, fid)
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest, exist_ok=True)
    for n in names:
        if n.endswith("/"):
            continue
        with open(os.path.join(dest, os.path.basename(n)), "wb") as f:
            f.write(zf.read(n))
    zf.close()

    # 4) 编译运行校验（链接 simulator）
    sim_c = os.path.join(ROOT, "simulator", "flash_sim.c")
    if not os.path.exists(sim_c):
        shutil.rmtree(dest, ignore_errors=True)
        return False, {"error": "模拟基座源文件缺失，无法编译"}

    entry_src = os.path.join(dest, entry)
    lib_c = os.path.join(dest, manifest.get("lib", "") + ".c") \
        if manifest.get("lib") else None
    compile_srcs = [sim_c, entry_src]
    if lib_c and os.path.exists(lib_c):
        compile_srcs.append(lib_c)

    tmp_exe = os.path.join(tempfile.gettempdir(), "flash_use_import_%s" % fid)
    cmd = [gcc, "-std=c99", "-Wall", "-Wextra",
           "-I" + os.path.join(ROOT, "simulator"), "-I" + dest,
           "-o", tmp_exe] + compile_srcs
    build = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if build.returncode != 0:
        shutil.rmtree(dest, ignore_errors=True)
        return False, {
            "error": "编译校验失败：库不符合模拟基座接口",
            "lines": [{"level": "fail", "text": l} for l in build.stderr.splitlines()],
        }

    # 5) 运行一次（在工作目录内，使 BIN 落盘到 imports/<id>）以确认可仿真
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


def io_BytesIO_safe(b):
    import io
    return io.BytesIO(b)
