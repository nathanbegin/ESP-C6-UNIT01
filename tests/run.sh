#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_bin=$(mktemp)
trap 'rm -f "$test_bin"' EXIT
cc -std=c11 -Wall -Wextra -Werror -pedantic -I main main/fan_controller.c tests/test_fan_controller.c -o "$test_bin"
"$test_bin"
node --check c6_door_sensor.js
node --check c6_door_sensor.mjs
node tests/test_converter.cjs
