#!/usr/bin/env bash
# Ensemble Build Wrapper
# Forwards arguments directly to the unified Python build orchestrator build.py

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${SCRIPT_DIR}/build.py" "$@"