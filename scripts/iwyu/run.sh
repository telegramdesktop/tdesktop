#!/usr/bin/env bash
# Sources project-root .env then runs scripts/iwyu/run_iwyu.py.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo_root"

if [ -f .env ]; then
    # shellcheck disable=SC1091
    source .env
fi

exec python3 scripts/iwyu/run_iwyu.py "$@"
