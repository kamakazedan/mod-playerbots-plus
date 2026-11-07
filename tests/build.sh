#!/usr/bin/env bash
# Builds and runs the pricing tests. Needs only g++: the pricing has no game
# types in it. Run from anywhere: bash tests/build.sh
set -e
T=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$T/market_price_test}
g++ -std=c++20 -O0 -g -Wall -Wextra -I"$T/../src" \
    "$T/market_price_test.cpp" "$T/../src/PlayerbotsPlusMarketPrice.cpp" -o "$OUT"
"$OUT"
