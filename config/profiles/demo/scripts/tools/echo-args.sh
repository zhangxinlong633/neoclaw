#!/usr/bin/env bash
set -euo pipefail
# Read stdin JSON (or empty) and echo it; also print NEO_TOOL_NAME.
echo "name=${NEO_TOOL_NAME:-}"
if [[ -n "${NEO_TOOL_ARGS:-}" ]]; then
  echo "env=$NEO_TOOL_ARGS"
else
  echo -n "stdin="
  cat
  echo
fi
