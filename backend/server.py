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
FRAMEWORKS = [
    {
        "id": "simulator",
        "name": "模拟基座 (NOR/NAND/EEPROM)",
        "desc": "Flash 物理特性模拟：按块擦除、写入仅允许 1->0、EEPROM 字节写、"
                "寿命统计。自检覆盖 NOR 写入/读取/掉电拒绝与 EEPROM 读写。",
        "sources": ["simulator/flash_sim.c", "simulator/test/main_sim.c"],
        "includes": ["simulator"],
        "workdir": "simulator/test",
    },
    {
        "id": "kv",
        "name": "KV/NVS 存储框架",
        "desc": "基于模拟基座的键值存储：两步提交掉电安全、CRC 校验、后写覆盖、"
                "删除、压实垃圾回收。验证写入/读取/更新/删除/掉电残留丢弃/GC。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/kv/kv_store.c",
            "frameworks/kv/test/main_kv.c",
        ],
        "includes": ["simulator", "frameworks/kv"],
        "workdir": "frameworks/kv/test",
    },
    # 后续框架（fs / baremetal / ota）在此追加注册即可被前端发现
]


def get_framework(fid):
    for f in FRAMEWORKS:
        if f["id"] == fid:
            return f
    return None


def parse_output(text):
    """将 C 测试程序 stdout 解析为结构化行，并判定整体成功/失败。"""
    lines = []
    ok_count = 0
    fail_count = 0
    for raw in text.splitlines():
        line = raw.rstrip("\n")
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
    # 整体判定：以程序退出码优先；无退出码信息时用"存在失败"字样兜底
    success = fail_count == 0 and ("全部通过" in text or ok_count > 0)
    return {
        "lines": lines,
        "ok_count": ok_count,
        "fail_count": fail_count,
        "success": success,
    }


def run_framework(fid):
    """编译并运行指定框架测试，返回结果字典。"""
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
        # 编译失败：返回编译错误输出
        err_lines = [{"level": "fail", "text": l}
                     for l in build.stderr.splitlines()]
        return {
            "success": False,
            "error": "编译失败",
            "lines": err_lines,
        }

    # 运行（在工作目录内，使 BIN 落盘到框架 test 目录）
    try:
        t0 = time.time()
        run = subprocess.run([tmp_exe], cwd=workdir,
                             capture_output=True, text=True, timeout=60)
        elapsed = time.time() - t0
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "运行超时", "lines": []}

    parsed = parse_output(run.stdout)
    parsed["elapsed_ms"] = int(elapsed * 1000)
    parsed["return_code"] = run.returncode
    # 程序非零退出码表示存在失败
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
            self._send_json({
                "frameworks": [
                    {"id": f["id"], "name": f["name"], "desc": f["desc"]}
                    for f in FRAMEWORKS
                ]
            })
        else:
            self.send_error(404, "Not Found")

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path != "/api/run":
            self.send_error(404, "Not Found")
            return
        length = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(length) if length else b"{}"
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            payload = {}
        fid = payload.get("framework", "")
        result = run_framework(fid)
        self._send_json(result)


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
