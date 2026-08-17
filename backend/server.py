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


def find_gcc():
    """定位 gcc 可执行文件。"""
    for cand in ("gcc", "/usr/bin/gcc", "/usr/local/bin/gcc"):
        from shutil import which
        p = which(cand)
        if p:
            return p
    return None


# ---------------------------------------------------------------------------
# 框架注册表：描述每个可测试框架的源码组成与编译方式。
# 每个框架：
#   id        唯一标识
#   name      展示名
#   desc      简介
#   sources   参与编译的 C 源文件（相对 ROOT）
#   includes  编译 -I 包含目录（相对 ROOT）
#   workdir   测试运行时的工作目录（用于放置 BIN，相对 ROOT）
# ---------------------------------------------------------------------------
# 模拟基座可配置项 -> 环境变量名（所有框架共用，注入到测试程序）
SIM_ENV_MAP = {
    "type": "SIM_TYPE",
    "total": "SIM_TOTAL",
    "erase_size": "SIM_ERASE",
    "write_size": "SIM_WRITE",
    "read_us": "SIM_RD_US",
    "write_us": "SIM_WR_US",
    "erase_us": "SIM_ERASE_US",
    "erase_cycles": "SIM_CYCLES",
    "bad_blocks": "SIM_BAD_N",
    "bad_ratio": "SIM_BAD_R",
}
# KV 测试可配置项 -> 环境变量名
KV_ENV_MAP = {
    "capacity": "KV_CAPACITY",
    "func": "KV_FUNC",
    "n": "KV_N",
    "vlen": "KV_VLEN",
    "rounds": "KV_ROUNDS",
    "modfreq": "KV_MODFREQ",
}

# 配置项 schema（前端渲染表单用）：group 区分"模拟基座"与"测试"
SIM_CONFIG_SCHEMA = [
    {"key": "type", "label": "存储介质类型", "type": "select",
     "options": [["0", "NOR"], ["1", "NAND"], ["2", "EEPROM"]], "default": "0",
     "group": "simulator"},
    {"key": "total", "label": "总容量(字节)", "type": "number",
     "default": 65536, "min": 1024, "step": 1024, "group": "simulator"},
    {"key": "erase_size", "label": "擦除块大小(字节)", "type": "number",
     "default": 4096, "min": 256, "step": 256, "group": "simulator"},
    {"key": "write_size", "label": "最小写入单位(字节)", "type": "number",
     "default": 1, "min": 1, "step": 1, "group": "simulator"},
    {"key": "erase_cycles", "label": "标称擦写寿命(次)", "type": "number",
     "default": 100000, "min": 1, "step": 1000, "group": "simulator"},
    {"key": "read_us", "label": "读耗时(us/次)", "type": "number",
     "default": 0, "min": 0, "step": 1, "group": "simulator"},
    {"key": "write_us", "label": "写耗时(us/次)", "type": "number",
     "default": 0, "min": 0, "step": 1, "group": "simulator"},
    {"key": "erase_us", "label": "擦除耗时(us/次)", "type": "number",
     "default": 0, "min": 0, "step": 1, "group": "simulator"},
    {"key": "bad_blocks", "label": "固定坏块数量", "type": "number",
     "default": 0, "min": 0, "step": 1, "group": "simulator"},
    {"key": "bad_ratio", "label": "运行时坏块比率(万分之)", "type": "number",
     "default": 0, "min": 0, "max": 10000, "step": 1, "group": "simulator"},
]
KV_TEST_CONFIG_SCHEMA = [
    {"key": "func", "label": "功能压测模式", "type": "select",
     "options": [["0", "一致性自检"], ["1", "功能压测"]], "default": "0",
     "group": "test"},
    {"key": "capacity", "label": "KV 区容量(字节)", "type": "number",
     "default": 8192, "min": 1024, "step": 1024, "group": "test"},
    {"key": "n", "label": "条目数量", "type": "number",
     "default": 50, "min": 1, "step": 1, "group": "test"},
    {"key": "vlen", "label": "每条 value 长度(字节)", "type": "number",
     "default": 32, "min": 1, "step": 1, "group": "test"},
    {"key": "rounds", "label": "修改轮数", "type": "number",
     "default": 20, "min": 1, "step": 1, "group": "test"},
    {"key": "modfreq", "label": "每轮修改比例(%)", "type": "number",
     "default": 50, "min": 0, "max": 100, "step": 1, "group": "test"},
]

FRAMEWORKS = [
    {
        "id": "simulator",
        "name": "模拟基座 (NOR/NAND/EEPROM)",
        "desc": "Flash 物理特性模拟：按块擦除、写入仅允许 1->0、EEPROM 字节写、"
                "寿命统计、坏块模拟。可配置类型/容量/块大小/速度/坏点。",
        "sources": ["simulator/flash_sim.c", "simulator/test/main_sim.c"],
        "includes": ["simulator"],
        "workdir": "simulator/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [],
    },
    {
        "id": "kv",
        "name": "KV/NVS 存储框架",
        "desc": "基于模拟基座的键值存储：两步提交掉电安全、CRC 校验、后写覆盖、"
                "删除、压实垃圾回收。支持功能压测（条目数/长度/修改频率）。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/kv/kv_store.c",
            "frameworks/kv/test/main_kv.c",
        ],
        "includes": ["simulator", "frameworks/kv"],
        "workdir": "frameworks/kv/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": KV_TEST_CONFIG_SCHEMA,
    },
    # 后续框架（fs / baremetal / ota）在此追加注册即可被前端发现
]


def get_framework(fid):
    for f in FRAMEWORKS:
        if f["id"] == fid:
            return f
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
        elif "[FAIL]" in line or "[FAIL]" in line:
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


def _build_env(config, test_config):
    """根据前端传入的配置构造测试程序的环境变量（注入 SIM_*/KV_*）。"""
    env = dict(os.environ)
    if config:
        for k, envk in SIM_ENV_MAP.items():
            if k in config and config[k] != "":
                env[envk] = str(config[k])
    if test_config:
        for k, envk in KV_ENV_MAP.items():
            if k in test_config and test_config[k] != "":
                env[envk] = str(test_config[k])
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
    workdir = os.path.join(ROOT, fw["workdir"])
    os.makedirs(workdir, exist_ok=True)

    tmp_exe = os.path.join(tempfile.gettempdir(), "flash_use_%s_test" % fid)
    cmd = [gcc, "-std=c99", "-Wall", "-Wextra"] + inc_flags + \
          ["-o", tmp_exe] + src_paths

    try:
        build = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "编译超时", "lines": []}

    if build.returncode != 0:
        err_lines = [{"level": "fail", "text": l}
                     for l in build.stderr.splitlines()]
        return {"success": False, "error": "编译失败", "lines": err_lines}

    try:
        t0 = time.time()
        run = subprocess.run([tmp_exe], cwd=workdir, env=_build_env(config, test_config),
                             capture_output=True, text=True, timeout=120)
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
        return {"success": False, "error": "编译失败",
                "lines": [{"level": "fail", "text": l}
                          for l in build.stderr.splitlines()]}

    try:
        t0 = time.time()
        run = subprocess.run([tmp_exe], cwd=dest, env=_build_env(config, test_config),
                             capture_output=True, text=True, timeout=120)
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
                 "config_schema": f.get("config_schema", []),
                 "test_schema": f.get("test_schema", [])}
                for f in FRAMEWORKS
            ]
            imported = importer.imported_frameworks()
            self._send_json({"frameworks": builtin + imported})
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
