#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
scripts/run_tests.py - 一键编译并运行全部框架测试

复用 backend/registry.py 的框架注册表（单一事实来源），对每个注册框架：
  1. 按注册的 sources / includes 用 gcc 编译；
  2. 在注册的 workdir（BIN 落盘目录）运行；
  3. 检查退出码与输出中的"全部通过"字样。

模拟基座框架额外覆盖 NOR / NAND / EEPROM 三类介质（NAND 用小容量 +
受限测试项避免全片写满超时）。

用法：
  python3 scripts/run_tests.py            # 运行全部
  python3 scripts/run_tests.py <fid>...   # 只运行指定框架
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
import registry  # noqa: E402

GCC = os.environ.get("CC", "gcc")
CFLAGS = ["-std=c99", "-Wall", "-Wextra", "-D__USE_MINGW_ANSI_STDIO=1"]
TIMEOUT_BUILD = 60
TIMEOUT_RUN = 120


def _run(cmd, cwd, env=None, timeout=TIMEOUT_RUN):
    """运行命令，返回 (returncode, stdout, stderr)。"""
    try:
        proc = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True,
                              text=True, encoding="utf-8", errors="replace",
                              timeout=timeout)
        return proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "TIMEOUT(%ss)" % timeout


def compile_framework(fw, out_exe):
    """按注册表编译框架测试程序，返回 (ok, stderr)。"""
    src_paths = [os.path.join(ROOT, s) for s in fw["sources"]]
    for p in src_paths:
        if not os.path.exists(p):
            return False, "源文件缺失: %s" % p
    inc_flags = ["-I" + os.path.join(ROOT, d) for d in fw["includes"]]
    cmd = [GCC] + CFLAGS + inc_flags + ["-o", out_exe] + src_paths
    rc, _, err = _run(cmd, ROOT, timeout=TIMEOUT_BUILD)
    return rc == 0, err


def run_framework(fw, out_exe, env_extra=None):
    """运行框架测试程序，返回 (ok, summary, tail)。"""
    workdir = os.path.join(ROOT, fw["workdir"])
    os.makedirs(workdir, exist_ok=True)
    env = dict(os.environ)
    if env_extra:
        env.update(env_extra)
    rc, out, err = _run([out_exe], workdir, env=env)
    ok = (rc == 0) and ("全部通过" in out)
    # 汇总关键信息：结果行 + 最近的 3 行输出
    lines = [l for l in out.splitlines() if l.strip()]
    summary = [l for l in lines
               if "全部通过" in l or "存在失败" in l or "[FAIL]" in l]
    tail = lines[-3:]
    if err.strip():
        tail = tail + err.strip().splitlines()[-3:]
    return ok, summary, tail


def main():
    ids = sys.argv[1:] or None
    frameworks = [f for f in registry.FRAMEWORKS
                  if ids is None or f["id"] in ids]

    # 模拟基座额外覆盖 NAND/EEPROM（受限测试项 + 小容量，避免全片写满）
    extra_runs = {
        "simulator": [
            {"name": "simulator[NAND]", "env": {
                "SIM_TYPE": "1", "SIM_TOTAL": str(16 * 1024 * 1024),
                "SIM_TESTS": "basic,powerloss"}},
            {"name": "simulator[EEPROM]", "env": {
                "SIM_TYPE": "2", "SIM_TESTS": "basic,powerloss"}},
        ],
    }

    results = []
    failed = 0
    for fw in frameworks:
        exe = os.path.join(tempfile.gettempdir(), "flash_use_%s_test" % fw["id"])
        ok, err = compile_framework(fw, exe)
        if not ok:
            print("[FAIL] %-24s 编译失败" % fw["id"])
            for l in err.splitlines()[:5]:
                print("       %s" % l)
            failed += 1
            results.append((fw["id"], False, ["编译失败"], []))
            continue
        ok, summary, tail = run_framework(fw, exe)
        print("[%s] %-24s 运行完成" % ("OK  " if ok else "FAIL", fw["id"]))
        for s in summary:
            print("       %s" % s)
        if not ok:
            failed += 1
        results.append((fw["id"], ok, summary, tail))

        # 额外介质覆盖
        for extra in extra_runs.get(fw["id"], []):
            ok, summary, tail = run_framework(fw, exe, env_extra=extra["env"])
            print("[%s] %-24s 运行完成" % ("OK  " if ok else "FAIL", extra["name"]))
            for s in summary:
                print("       %s" % s)
            if not ok:
                failed += 1
            results.append((extra["name"], ok, summary, tail))

    print("\n================ 测试汇总 ================")
    for name, ok, _, _ in results:
        print("[%s] %s" % ("PASS" if ok else "FAIL", name))
    print("===========================================")
    print("通过 %d/%d" % (sum(1 for _, ok, _, _ in results if ok), len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
