#!/bin/sh
# First run fails after creating flag; second run appends one line.
FLAG=tests/fixtures/flaky.flag
OUT=tests/fixtures/count.out
if [ ! -f "$FLAG" ]; then
  touch "$FLAG"
  exit 1
fi
echo flaky_ok >> "$OUT"
exit 0
