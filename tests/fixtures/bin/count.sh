#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
echo x >> "$DIR/../count.out"
echo "counted"
