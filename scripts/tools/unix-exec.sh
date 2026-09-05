#!/usr/bin/env bash
# neo unix-exec：在固定二进制后附加 NEO_TOOL_ARGS.argv，不做 shell 拼接。
# 能力 argv 形如：["./scripts/tools/unix-exec.sh", "/usr/bin/wc"]
# NEO_TOOL_ARGS 示例：{"argv":["-l","README.md"]}
# 约束：额外参数不得含空字节；路径参数须相对且无 ..；选项须匹配安全模式。
set -euo pipefail
if [[ $# -lt 1 ]]; then
  echo "ERROR: unix-exec missing binary path" >&2
  exit 2
fi
BIN="$1"
if [[ "$BIN" != /* ]]; then
  echo "ERROR: binary must be absolute" >&2
  exit 2
fi
if [[ ! -x "$BIN" ]]; then
  echo "ERROR: binary not executable: $BIN" >&2
  exit 2
fi
export NEO_UNIX_BIN="$BIN"
exec python3 - <<'PY'
import json, os, re, sys

bin_path = os.environ["NEO_UNIX_BIN"]
raw = os.environ.get("NEO_TOOL_ARGS") or "{}"
try:
    obj = json.loads(raw)
except json.JSONDecodeError:
    print("ERROR: bad NEO_TOOL_ARGS JSON", file=sys.stderr)
    sys.exit(2)
extra = obj.get("argv") if isinstance(obj, dict) else None
if extra is None:
    extra = []
if not isinstance(extra, list):
    print("ERROR: argv must be an array", file=sys.stderr)
    sys.exit(2)

flag_re = re.compile(r"^-[A-Za-z0-9][A-Za-z0-9,.=_-]*$|^--[A-Za-z0-9][A-Za-z0-9=-_]*$")
out = []
for a in extra:
    if not isinstance(a, str) or a == "" or "\0" in a:
        print("ERROR: invalid argv entry", file=sys.stderr)
        sys.exit(2)
    if a.startswith("-"):
        if not flag_re.match(a):
            print(f"ERROR: disallowed flag: {a}", file=sys.stderr)
            sys.exit(2)
        out.append(a)
        continue
    if a.startswith("/") or a.startswith("~"):
        print("ERROR: absolute paths not allowed in argv", file=sys.stderr)
        sys.exit(2)
    parts = a.split("/")
    if any(p == ".." for p in parts):
        print("ERROR: .. not allowed in argv paths", file=sys.stderr)
        sys.exit(2)
    out.append(a)

os.execv(bin_path, [bin_path] + out)
PY
