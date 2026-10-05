# tests/unit/run.sh
#!/usr/bin/env bash
# Build and run every pure-logic unit test (no AzerothCore needed). Usage: bash tests/unit/run.sh
set -uo pipefail
cd "$(dirname "$0")"
mkdir -p bin
CXX=${CXX:-c++}
failed=0
for t in test_*.cpp; do
  name=${t%.cpp}
  if ! "$CXX" -std=c++20 -Wall -Wextra -Werror -O0 -g -o "bin/$name" "$t"; then
    echo "UNIT BUILD FAILED $name"; failed=$((failed + 1)); continue
  fi
  if "./bin/$name"; then echo "PASS $name"; else echo "FAIL $name"; failed=$((failed + 1)); fi
done
if [ "$failed" -eq 0 ]; then echo "UNIT ALL PASS"; exit 0; fi
echo "UNIT $failed FAILED"; exit 1
