#!/usr/bin/env bash
# scripts/run_tests.sh - 一键编译并运行全部框架测试
#
# 用法：
#   ./scripts/run_tests.sh              # 运行全部框架测试
#   ./scripts/run_tests.sh kv easyflash # 只运行指定框架
#
# 底层复用 scripts/run_tests.py（数据来自 backend/registry.py 注册表）。
set -eu

cd "$(dirname "$0")/.."
exec python3 scripts/run_tests.py "$@"
