#!/usr/bin/env bash
# Thin shim for scripts/iwyu/run_iwyu.py.
# Sources the project-root .env (if any) so locally-set vars like
# TDESKTOP_API_ID / DYLD_LIBRARY_PATH are available to the build.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo_root"

if [ -f .env ]; then
    # shellcheck disable=SC1091
    source .env
fi

exec python3 scripts/iwyu/run_iwyu.py "$@"
