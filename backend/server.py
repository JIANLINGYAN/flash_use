#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
backend/server.py - 模拟运行后端服务（模块三：仿真服务 + 模块四后端支撑）

职责：
  1. 提供前端静态页面（frontend/）。
  2. 提供 API 列出可测试框架（frameworks 注册表）。
  3. 接收前端"运行指定框架测试"的请求，在本地用 gcc 编译并运行对应 C
     测试程序（对接 simulator 与 frameworks 的源码），解析输出并返回结构化结果。

设计原则：仅依赖 Python 标准库，可直接 `python3 server.py` 运行，无需联网安装。
真实架构对应：前端 -> 后端(API) -> 模拟基座(C 测试程序) -> BIN 物理介质。

启动：  python3 server.py [--host 0.0.0.0] [--port 8000]
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

# 项目根目录（backend/ 的上一级）
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRONTEND_DIR = os.path.join(ROOT, "frontend")
SIMULATOR_DIR = os.path.join(ROOT, "simulator")
IMPORTS_DIR = os.path.join(ROOT, "imports")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import generator
import importer
from registry import (FRAMEWORKS, SIM_ENV_MAP, KV_ENV_MAP,
                      SIM_TYPE_DEFAULTS, _SIM_SKIP_IF_ZERO, get_framework,
                      app_layer_for, app_supported, APP_TASK_SCHEMA)


def find_gcc():
    """定位 gcc 可执行文件。"""
    for cand in ("gcc", "/usr/bin/gcc", "/usr/local/bin/gcc"):
        from shutil import which
        p = which(cand)
        if p:
            return p
    return None


def parse_output(text):
    """将 C 测试程序 stdout 解析为结构化行，并抽取统计与磨损图。"""
    lines = []
    ok_count = 0
    fail_count = 0
    stats = None
    wearmap = None
    for raw in text.splitlines():
        line = raw.rstrip("\n")
        if line.startswith("STATS_JSON:"):
            try:
                stats = json.loads(line[len("STATS_JSON:"):])
            except ValueError:
                pass
            continue
        if line.startswith("WEARMAP:"):
            wearmap = [int(x) for x in line[len("WEARMAP:"):].split(",") if x != ""]
            continue
        if "[OK]" in line or "[OK  ]" in line:
            level = "ok"
            ok_count += 1
        elif "[FAIL]" in line:
            level = "fail"
            fail_count += 1
        elif "全部通过" in line:
            level = "summary-ok"
        elif "存在失败" in line:
            level = "summary-fail"
        else:
            level = "info"
        lines.append({"level": level, "text": line})
    success = fail_count == 0 and ("全部通过" in text or ok_count > 0)
    return {
        "lines": lines,
        "ok_count": ok_count,
        "fail_count": fail_count,
        "success": success,
        "stats": stats,
        "wearmap": wearmap,
    }


def build_app_env(fid, config, app_config):
    """构造应用层测试环境变量：SIM_*（模拟基座）+ APP_*（应用层任务选项）。"""
    env = dict(os.environ)
    if config:
        for k, envk in SIM_ENV_MAP.items():
            if k in config and config[k] != "":
                v = config[k]
                if k in _SIM_SKIP_IF_ZERO and str(v) == "0":
                    continue
                env[envk] = str(v)
    app_env_map = {
        "task": "APP_TASK",
        "items": "APP_ITEMS",
        "vlen": "APP_VLEN",
        "rounds": "APP_ROUNDS",
        "freq": "APP_FREQ",
        "capacity": "APP_CAPACITY",
    }
    env["APP_COMPONENT"] = str(fid)
    if app_config:
        for k, envk in app_env_map.items():
            if k in app_config and app_config[k] != "":
                env[envk] = str(app_config[k])
    return env


def run_app_framework(fid, config=None, app_config=None):
    """编译并运行"应用层测试"（app/ 统一任务引擎 + 适配层 + 组件）。"""
    layer = app_layer_for(fid)
    if not layer:
        return {"success": False, "error": "组件不支持应用层测试: %s" % fid,
                "lines": []}
    fw = get_framework(fid)

    gcc = find_gcc()
    if not gcc:
        return {"success": False, "error": "未找到 gcc 编译器", "lines": []}

    src_paths = [os.path.join(ROOT, s) for s in layer["sources"]]
    for p in src_paths:
        if not os.path.exists(p):
            return {"success": False, "error": "源文件缺失: %s" % p, "lines": []}

    inc_flags = ["-I" + os.path.join(ROOT, d) for d in layer["includes"]]
    cflags = []
    for i, a in enumerate(layer["cflags"]):
        if a == "-include" and i + 1 < len(layer["cflags"]):
            cflags.append(a)
            cflags.append(os.path.join(ROOT, layer["cflags"][i + 1]))
        elif a.startswith("-D"):
            cflags.append(a)
    workdir = os.path.join(ROOT, fw["workdir"])
    os.makedirs(workdir, exist_ok=True)

    tmp_exe = os.path.join(tempfile.gettempdir(),
                           "flash_use_app_%s" % fid)
    cmd = [gcc, "-std=c99", "-Wall", "-Wextra",
           "-D__USE_MINGW_ANSI_STDIO=1",
           "-D_POSIX_C_SOURCE=199309L"] + cflags + inc_flags + \
          ["-o", tmp_exe] + src_paths

    try:
        build = subprocess.run(cmd, capture_output=True, text=True,
                               encoding="utf-8", errors="replace", timeout=60)
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "编译超时", "lines": []}
    if build.returncode != 0:
        return {"success": False, "error": "编译失败",
                "lines": [{"level": "fail", "text": l}
                          for l in build.stderr.splitlines()]}

    try:
        t0 = time.time()
        run = subprocess.run([tmp_exe], cwd=workdir,
                             env=build_app_env(fid, config, app_config),
                             capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=180)
        elapsed = time.time() - t0
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "运行超时", "lines": []}

    parsed = parse_output(run.stdout)
    parsed["elapsed_ms"] = int(elapsed * 1000)
    parsed["return_code"] = run.returncode
    if run.returncode != 0:
        parsed["success"] = False
    if run.stderr.strip():
        for l in run.stderr.splitlines():
            parsed["lines"].append({"level": "stderr", "text": l})
    return parsed


def run_app_framework_stream(fid, config=None, app_config=None):
    """应用层测试的流式运行生成器（build -> log -> done）。"""
    layer = app_layer_for(fid)
    if not layer:
        yield {"event": "done", "result": {"success": False,
                                           "error": "组件不支持应用层测试: %s" % fid,
                                           "lines": []}}
        return
    fw = get_framework(fid)
    src_paths = [os.path.join(ROOT, s) for s in layer["sources"]]
    for p in src_paths:
        if not os.path.exists(p):
            yield {"event": "done", "result": {"success": False,
                                               "error": "源文件缺失: %s" % p,
                                               "lines": []}}
            return
    exe, build = _compile_exe(src_paths, layer["includes"], "app_" + fid,
                              cflags=layer["cflags"])
    if not exe or build is None:
        yield {"event": "log", "level": "stderr",
               "text": "未找到 gcc 编译器，无法编译"}
        yield {"event": "done", "result": {"success": False,
                                           "error": "未找到 gcc 编译器",
                                           "lines": []}}
        return
    if build.returncode != 0:
        for l in build.stderr.splitlines():
            yield {"event": "log", "level": "fail", "text": l}
        yield {"event": "done", "result": {"success": False,
                                           "error": "编译失败", "lines": []}}
        return
    workdir = os.path.join(ROOT, fw["workdir"])
    os.makedirs(workdir, exist_ok=True)
    yield {"event": "log", "level": "info",
           "text": "[build] 应用层测试编译成功，开始运行…"}
    for ev in _stream_run(exe, workdir, build_app_env(fid, config, app_config),
                          "app_" + fid):
        yield ev


def _build_env(fw, config, test_config):
    """根据前端传入的配置构造测试程序的环境变量（注入 SIM_*/KV_*/FLT_* 等）。

    数值字段在 schema 默认值=0（"按类型默认"）时不注入环境变量，
    避免被 C 程序当成合法值（如 SIM_TOTAL=0）覆盖自身兜底默认。
    """
    env = dict(os.environ)
    if config:
        for k, envk in SIM_ENV_MAP.items():
            if k in config and config[k] != "":
                v = config[k]
                if k in _SIM_SKIP_IF_ZERO and str(v) == "0":
                    # 视为未配置，让 C 程序走兜底默认（或模拟器 FLASH_CFG_DEFAULTS_BY_TYPE）
                    continue
                env[envk] = str(v)
    if test_config:
        for k, envk in KV_ENV_MAP.items():
            if k not in test_config or test_config[k] == "":
                continue
            v = test_config[k]
            if k == "tests" and isinstance(v, list):
                env[envk] = ",".join(str(x) for x in v)
            elif k == "items" and isinstance(v, list):
                # 每条目序列化为 LEN,N,FREQ
                parts = []
                for it in v:
                    parts.append("%s,%s,%s" % (it.get("vlen", 0),
                                              it.get("n", 0),
                                              it.get("freq", 0)))
                env[envk] = ";".join(parts)
            else:
                env[envk] = str(v)
    # 通用测试项环境变量映射：各框架注册项通过 test_env 声明
    # 配置 key -> 环境变量名（如 fastflash: tests -> FLT_TESTS）
    test_env = (fw or {}).get("test_env")
    if test_env and test_config:
        for k, envk in test_env.items():
            if k not in test_config or test_config[k] == "":
                continue
            v = test_config[k]
            if isinstance(v, list):
                env[envk] = ",".join(str(x) for x in v)
            else:
                env[envk] = str(v)
    return env


def run_framework(fid, config=None, test_config=None):
    """编译并运行指定框架测试，返回结果字典。支持内置与已导入框架。"""
    # 已导入的自定义框架
    reg = importer.load_registry()
    if fid in reg:
        return _run_imported(fid, reg[fid], config, test_config)

    fw = get_framework(fid)
    if not fw:
        return {"success": False, "error": "未知框架: %s" % fid, "lines": []}

    gcc = find_gcc()
    if not gcc:
        return {"success": False, "error": "未找到 gcc 编译器", "lines": []}

    src_paths = [os.path.join(ROOT, s) for s in fw["sources"]]
    for p in src_paths:
        if not os.path.exists(p):
            return {"success": False, "error": "源文件缺失: %s" % p, "lines": []}

    inc_flags = ["-I" + os.path.join(ROOT, d) for d in fw["includes"]]
    cflags = []
    for i, a in enumerate(fw.get("cflags", [])):
        if a == "-include" and i + 1 < len(fw.get("cflags", [])):
            cflags.append(a)
            cflags.append(os.path.join(ROOT, fw["cflags"][i + 1]))
        elif a.startswith("-D"):
            cflags.append(a)
    workdir = os.path.join(ROOT, fw["workdir"])
    os.makedirs(workdir, exist_ok=True)

    tmp_exe = os.path.join(tempfile.gettempdir(), "flash_use_%s_test" % fid)
    cmd = [gcc, "-std=c99", "-Wall", "-Wextra",
           "-D__USE_MINGW_ANSI_STDIO=1"] + cflags + inc_flags + \
          ["-o", tmp_exe] + src_paths

    try:
        build = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "编译超时", "lines": []}

    if build.returncode != 0:
        err_lines = [{"level": "fail", "text": l}
                     for l in build.stderr.splitlines()]
        return {"success": False, "error": "编译失败", "lines": err_lines}

    try:
        t0 = time.time()
        run = subprocess.run([tmp_exe], cwd=workdir, env=_build_env(fw, config, test_config),
                             capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
        elapsed = time.time() - t0
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "运行超时", "lines": []}

    parsed = parse_output(run.stdout)
    parsed["elapsed_ms"] = int(elapsed * 1000)
    parsed["return_code"] = run.returncode
    if run.returncode != 0:
        parsed["success"] = False
    if run.stderr.strip():
        for l in run.stderr.splitlines():
            parsed["lines"].append({"level": "stderr", "text": l})
    return parsed


# ---------------------------------------------------------------------------
# 流式运行（SSE）：边编译边运行，实时把输出推给前端，避免长时间测试时
# 用户只能干等最终结果。生成器 yield 事件字典：
#   {"event": "build",  "text": <编译日志行>}
#   {"event": "log",    "level": <info|ok|fail|stderr>, "text": <运行日志行>}
#   {"event": "done",   "result": <汇总字典>}
# 速度控制：每条运行日志推送前小幅节流（~15ms），避免浏览器高频重绘卡顿。
# ---------------------------------------------------------------------------
def _compile_exe(sources, includes, fid, cflags=None):
    """编译测试程序，返回 (tmp_exe, build)。"""
    gcc = find_gcc()
    if not gcc:
        return None, None
    inc_flags = ["-I" + os.path.join(ROOT, d) for d in includes]
    extra = []
    for i, a in enumerate(cflags or []):
        if a == "-include" and i + 1 < len(cflags):
            extra.append(a)
            extra.append(os.path.join(ROOT, cflags[i + 1]))
        elif a.startswith("-D"):
            extra.append(a)
    tmp_exe = os.path.join(tempfile.gettempdir(), "flash_use_%s_test" % fid)
    cmd = [gcc, "-std=c99", "-Wall", "-Wextra",
           "-D__USE_MINGW_ANSI_STDIO=1"] + extra + inc_flags + \
          ["-o", tmp_exe] + sources
    try:
        build = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
    except subprocess.TimeoutExpired:
        return None, None
    return tmp_exe, build


def _classify_line(line):
    """根据行内容粗略判断日志等级（与 OK/FAIL 判定一致）。"""
    s = line.strip()
    if "[OK]" in s or "PASS" in s.upper() or s.startswith("✓"):
        return "ok"
    if "[FAIL]" in s or "[ERROR]" in s or "FAIL" in s.upper():
        return "fail"
    if "[WARN]" in s or "[WARNING]" in s:
        return "warn"
    return "info"


def _stream_run(exe, workdir, env, fid):
    """跨平台实时读取子进程输出（Windows / WSL / Linux 兼容）。

    不使用 select（Windows 上 select 不支持管道），改用后台线程读取
    子进程的 stdout/stderr 并放入队列，主循环通过 proc.poll() 判断结束。
    """
    import threading
    import queue

    try:
        t0 = time.time()
        proc = subprocess.Popen([exe], cwd=workdir, env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, encoding="utf-8", errors="replace")
    except OSError as e:
        yield {"event": "log", "level": "stderr", "text": "启动失败: %s" % e}
        yield {"event": "done", "result": {"success": False,
                                           "error": "启动失败: %s" % e, "lines": []}}
        return

    out_q = queue.Queue()
    err_q = queue.Queue()

    def _reader(stream, q):
        try:
            for line in iter(stream.readline, ""):
                q.put(line)
        except Exception:
            pass
        finally:
            try:
                stream.close()
            except Exception:
                pass

    t_out = threading.Thread(target=_reader, args=(proc.stdout, out_q), daemon=True)
    t_err = threading.Thread(target=_reader, args=(proc.stderr, err_q), daemon=True)
    t_out.start()
    t_err.start()

    def drain_out(throttle=True):
        while not out_q.empty():
            line = out_q.get().rstrip("\n")
            out_text.append(line + "\n")
            yield {"event": "log", "level": _classify_line(line), "text": line}
            if throttle:
                time.sleep(0.015)

    stderr_buf = []
    out_text = []
    # 主循环：在子进程运行期间持续从队列取输出，避免忙等也不阻塞
    while proc.poll() is None:
        for ev in drain_out():
            yield ev
        while not err_q.empty():
            stderr_buf.append(err_q.get().rstrip("\n"))
        time.sleep(0.02)

    # 子进程已结束，把残留输出全部取出
    for ev in drain_out(throttle=False):
        yield ev
    while not err_q.empty():
        stderr_buf.append(err_q.get().rstrip("\n"))
    t_out.join(timeout=1)
    t_err.join(timeout=1)

    for el in stderr_buf:
        yield {"event": "log", "level": "stderr", "text": el}

    # 汇总完整输出并解析出统计/磨损图等结构化结果
    raw = "".join(out_text)
    parsed = parse_output(raw if raw else None)
    parsed["return_code"] = proc.returncode
    parsed["success"] = proc.returncode == 0
    elapsed = time.time() - t0
    parsed["elapsed_ms"] = int(elapsed * 1000)
    yield {"event": "done", "result": parsed}


def run_framework_stream(fid, config=None, test_config=None):
    """流式运行生成器：先推 build 事件，再推 log 事件，最后推 done 事件。"""
    reg = importer.load_registry()
    builtin_ids = {f["id"] for f in FRAMEWORKS}
    if fid in reg:
        manifest = reg[fid]
        dest = os.path.join(IMPORTS_DIR, fid)
        sim_c = os.path.join(ROOT, "simulator", "flash_sim.c")
        entry_src = os.path.join(dest, manifest.get("entry", "test_main.c"))
        srcs = [sim_c, entry_src]
        lib_sources = manifest.get("lib_sources")
        if lib_sources:
            for ls in lib_sources:
                p = os.path.join(dest, ls)
                if os.path.exists(p):
                    srcs.append(p)
        else:
            lib_c = os.path.join(dest, manifest.get("lib", "") + ".c") \
                if manifest.get("lib") else None
            if lib_c and os.path.exists(lib_c):
                srcs.append(lib_c)
        exe, build = _compile_exe(srcs, [os.path.join(ROOT, "simulator"), dest], fid)
        workdir = dest
    else:
        fw = get_framework(fid)
        if not fw:
            yield {"event": "done", "result": {"success": False,
                                               "error": "未知框架: %s" % fid,
                                               "lines": []}}
            return
        src_paths = [os.path.join(ROOT, s) for s in fw["sources"]]
        for p in src_paths:
            if not os.path.exists(p):
                yield {"event": "done", "result": {"success": False,
                                                   "error": "源文件缺失: %s" % p,
                                                   "lines": []}}
                return
        exe, build = _compile_exe(src_paths, fw["includes"], fid,
                                  cflags=fw.get("cflags"))
        workdir = os.path.join(ROOT, fw["workdir"])

    if not exe or build is None:
        yield {"event": "log", "level": "stderr", "text": "未找到 gcc 编译器，无法编译"}
        yield {"event": "done", "result": {"success": False,
                                           "error": "未找到 gcc 编译器", "lines": []}}
        return
    if build.returncode != 0:
        for l in build.stderr.splitlines():
            yield {"event": "log", "level": "fail", "text": l}
        yield {"event": "done", "result": {"success": False, "error": "编译失败",
                                           "lines": []}}
        return

    yield {"event": "log", "level": "info", "text": "[build] 编译成功，开始运行…"}
    for ev in _stream_run(exe, workdir, _build_env(fw, config, test_config), fid):
        yield ev


def _run_imported(fid, manifest, config=None, test_config=None):
    """编译并运行已导入框架（位于 imports/<id>/）。"""
    gcc = find_gcc()
    if not gcc:
        return {"success": False, "error": "未找到 gcc 编译器", "lines": []}

    dest = os.path.join(IMPORTS_DIR, fid)
    if not os.path.isdir(dest):
        return {"success": False, "error": "导入框架目录缺失: %s" % fid, "lines": []}

    entry = manifest.get("entry", "test_main.c")
    sim_c = os.path.join(ROOT, "simulator", "flash_sim.c")
    entry_src = os.path.join(dest, entry)

    compile_srcs = [sim_c, entry_src]
    # 兼容旧导出包（单一 lib 文件）与新导出包（lib_sources 列表）
    lib_sources = manifest.get("lib_sources")
    if lib_sources:
        for ls in lib_sources:
            p = os.path.join(dest, ls)
            if os.path.exists(p):
                compile_srcs.append(p)
    else:
        lib_c = os.path.join(dest, manifest.get("lib", "") + ".c") \
            if manifest.get("lib") else None
        if lib_c and os.path.exists(lib_c):
            compile_srcs.append(lib_c)

    tmp_exe = os.path.join(tempfile.gettempdir(), "flash_use_import_%s" % fid)
    extra = []
    mflags = manifest.get("cflags", [])
    for i, a in enumerate(mflags):
        if a == "-include" and i + 1 < len(mflags):
            extra.append(a)
            extra.append(os.path.join(dest, os.path.basename(mflags[i + 1])))
        elif a.startswith("-D"):
            extra.append(a)
    cmd = [gcc, "-std=c99", "-Wall", "-Wextra",
           "-I" + os.path.join(ROOT, "simulator"), "-I" + dest,
           "-o", tmp_exe] + extra + compile_srcs

    build = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
    if build.returncode != 0:
        return {"success": False, "error": "编译失败",
                "lines": [{"level": "fail", "text": l}
                          for l in build.stderr.splitlines()]}

    try:
        t0 = time.time()
        run = subprocess.run([tmp_exe], cwd=dest, env=_build_env(None, config, test_config),
                             capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
        elapsed = time.time() - t0
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "运行超时", "lines": []}

    parsed = parse_output(run.stdout)
    parsed["elapsed_ms"] = int(elapsed * 1000)
    parsed["return_code"] = run.returncode
    if run.returncode != 0:
        parsed["success"] = False
    if run.stderr.strip():
        for l in run.stderr.splitlines():
            parsed["lines"].append({"level": "stderr", "text": l})
    return parsed


# ---------------------------------------------------------------------------
# HTTP 处理
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("[server] " + (fmt % args) + "\n")

    def _send_json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _stream_run_sse(self, fid, config, test_config):
        """SSE 流式运行：以 text/event-stream 实时推送编译/运行日志。"""
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Accel-Buffering", "no")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        try:
            for ev in run_framework_stream(fid, config, test_config):
                data = json.dumps(ev, ensure_ascii=False)
                # SSE 帧：event: <type>\ndata: <json>\n\n
                self.wfile.write(("event: %s\n" % ev.get("event", "log")).encode("utf-8"))
                self.wfile.write(("data: %s\n\n" % data).encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            return
        # 结束帧
        self.wfile.write(b"event: end\ndata: {}\n\n")
        self.wfile.flush()

    def _stream_run_sse_app(self, fid, config, app_config):
        """应用层测试 SSE 流式运行。"""
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Accel-Buffering", "no")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        try:
            for ev in run_app_framework_stream(fid, config, app_config):
                data = json.dumps(ev, ensure_ascii=False)
                self.wfile.write(("event: %s\n" % ev.get("event", "log")).encode("utf-8"))
                self.wfile.write(("data: %s\n\n" % data).encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            return
        self.wfile.write(b"event: end\ndata: {}\n\n")
        self.wfile.flush()

    def _send_file(self, path, ctype):
        try:
            with open(path, "rb") as f:
                body = f.read()
        except OSError:
            self.send_error(404, "Not Found")
            return
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_bytes(self, body, ctype, filename=None):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        if filename:
            self.send_header("Content-Disposition",
                             'attachment; filename="%s"' % filename)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        route = parsed.path
        if route in ("/", "/index.html"):
            self._send_file(os.path.join(FRONTEND_DIR, "index.html"),
                            "text/html; charset=utf-8")
        elif route == "/app.js":
            self._send_file(os.path.join(FRONTEND_DIR, "app.js"),
                            "application/javascript; charset=utf-8")
        elif route == "/style.css":
            self._send_file(os.path.join(FRONTEND_DIR, "style.css"),
                            "text/css; charset=utf-8")
        elif route == "/api/frameworks":
            builtin = [
                {"id": f["id"], "name": f["name"], "desc": f["desc"],
                 "category": f.get("category", ""),
                 "app_supported": app_supported(f["id"]),
                 "config_schema": f.get("config_schema", []),
                 "test_schema": f.get("test_schema", []),
                 "test_items": f.get("test_items", [])}
                for f in FRAMEWORKS
            ]
            imported = importer.imported_frameworks()
            # 把"按类型默认"映射一并返回，前端在切换 type 时用其刷新
            # 其他字段的当前值。
            defaults = {
                k: {"0": v[0], "1": v[1], "2": v[2]}
                for k, v in SIM_TYPE_DEFAULTS.items()
            }
            self._send_json(
                {"frameworks": builtin + imported,
                 "sim_type_defaults": defaults,
                 "app_task_schema": APP_TASK_SCHEMA}
            )
        else:
            self.send_error(404, "Not Found")

    def do_POST(self):
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(length) if length else b"{}"
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            payload = {}

        if parsed.path == "/api/run":
            fid = payload.get("framework", "")
            config = payload.get("config", {})
            test_config = payload.get("test_config", {})
            result = run_framework(fid, config, test_config)
            self._send_json(result)
        elif parsed.path == "/api/run/stream":
            fid = payload.get("framework", "")
            config = payload.get("config", {})
            test_config = payload.get("test_config", {})
            self._stream_run_sse(fid, config, test_config)
        elif parsed.path == "/api/app/run":
            fid = payload.get("framework", "")
            config = payload.get("config", {})
            app_config = payload.get("app_config", {})
            result = run_app_framework(fid, config, app_config)
            self._send_json(result)
        elif parsed.path == "/api/app/run/stream":
            fid = payload.get("framework", "")
            config = payload.get("config", {})
            app_config = payload.get("app_config", {})
            self._stream_run_sse_app(fid, config, app_config)
        elif parsed.path == "/api/generate":
            fid = payload.get("framework", "")
            params = payload.get("params", {})
            try:
                zip_bytes, manifest = generator.generate_zip(fid, params)
            except RuntimeError as e:
                self._send_json({"success": False, "error": str(e)}, code=400)
                return
            fname = "%s_library.zip" % manifest.get("lib", fid)
            self._send_bytes(zip_bytes, "application/zip", fname)
        elif parsed.path == "/api/import":
            file_b64 = payload.get("file_b64", "")
            if not file_b64:
                self._send_json({"success": False, "error": "未收到文件数据"}, code=400)
                return
            try:
                import base64
                zip_bytes = base64.b64decode(file_b64)
            except Exception:
                self._send_json({"success": False, "error": "文件解码失败"}, code=400)
                return
            ok, res = importer.import_zip(zip_bytes)
            self._send_json({"success": ok, **res},
                            code=200 if ok else 400)
        else:
            self.send_error(404, "Not Found")


def main():
    ap = argparse.ArgumentParser(description="Flash 模拟运行后端服务")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8000)
    args = ap.parse_args()

    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    print("Flash 模拟运行服务已启动: http://%s:%d" % (args.host, args.port))
    print("项目根目录: %s" % ROOT)
    print("按 Ctrl+C 退出")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止")
        httpd.shutdown()


if __name__ == "__main__":
    main()
