#!/usr/bin/env bash
# scripts/run_server.sh - 启动 Flash 模拟运行后端服务
#
# 用法：
#   ./scripts/run_server.sh              # 默认 0.0.0.0:8000
#   ./scripts/run_server.sh --port 9000  # 指定端口（透传给 backend/server.py）
#
# 启动后浏览器访问 http://localhost:<port>
set -eu

cd "$(dirname "$0")/.."
exec python3 backend/server.py "$@"
