#!/bin/sh
# Button masher for PAD_FILE runs: waits $1 s, then presses SQUARE most of the time,
# TRIANGLE every 4th and CIRCLE every 6th press, ~3 presses a second, until $2 s.
f="$3"; sleep "$1"; i=0; end=$(( $(date +%s) + $2 - $1 ))
while [ "$(date +%s)" -lt "$end" ]; do
  i=$((i+1)); m=0x8000
  [ $((i % 4)) -eq 0 ] && m=0x1000
  [ $((i % 6)) -eq 0 ] && m=0x2000
  echo "$m 4" > "$f"; sleep 0.33
done
