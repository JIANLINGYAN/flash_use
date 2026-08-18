#!/usr/bin/env python3
"""验证所有组件应用层测试能否编译（快速回归）。"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
from registry import FRAMEWORKS, app_layer_for, app_supported

GCC = os.environ.get("CC", "gcc")
# -D_POSIX_C_SOURCE: 应用层 app_util.c 使用 clock_gettime；某些组件
# （如 yaffs）用 -include 注入宿主类型头，会先于源文件锁定 glibc
# feature 宏，故须在命令行统一开启（对全平台无害）。
CFLAGS = ["-std=c99", "-Wall", "-Wextra", "-D__USE_MINGW_ANSI_STDIO=1",
          "-D_POSIX_C_SOURCE=199309L"]


def main():
    only = [a for a in sys.argv[1:] if not a.startswith("-")]
    failed = 0
    for fw in FRAMEWORKS:
        if not app_supported(fw["id"]):
            print("[skip] %-12s 无应用层适配器" % fw["id"])
            continue
        if only and fw["id"] not in only:
            continue
        layer = app_layer_for(fw["id"])
        src_paths = [os.path.join(ROOT, s) for s in layer["sources"]]
        missing = [s for s in src_paths if not os.path.exists(s)]
        if missing:
            print("[FAIL] %-12s 源文件缺失: %s" % (fw["id"], missing))
            failed += 1
            continue
        inc_flags = ["-I" + os.path.join(ROOT, d) for d in layer["includes"]]
        extra = []
        cflags = layer["cflags"]
        for i, a in enumerate(cflags):
            if a == "-include" and i + 1 < len(cflags):
                extra.append(a)
                extra.append(os.path.join(ROOT, cflags[i + 1]))
            elif a.startswith("-D"):
                extra.append(a)
        exe = os.path.join(tempfile.gettempdir(), "flash_use_app_check_%s" % fw["id"])
        cmd = [GCC] + CFLAGS + extra + inc_flags + ["-o", exe] + src_paths
        rc = subprocess.run(cmd, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
        if rc.returncode == 0:
            print("[ OK ] %-12s 编译成功" % fw["id"])
        else:
            print("[FAIL] %-12s 编译失败" % fw["id"])
            for l in rc.stderr.splitlines()[-8:]:
                print("       %s" % l)
            failed += 1
    print("\n编译失败组件数: %d" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
