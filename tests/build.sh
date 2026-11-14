#!/usr/bin/env bash
# Builds and runs the tests: the market's pricing, the text helpers with what
# the conf ships, the economy ledger's words and sums, and bot dungeons'
# choices (who goes, grey quests, wings, a full quest log), group quests'
# (party size, what to ask for, what next) and city life's sums (tempers,
# rare mounts, rings of people, hops). Needs only g++:
# none of them has game types in it. Run from anywhere: bash tests/build.sh
# (OUT=<dir> puts the binaries elsewhere).
set -e
T=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$T/.." && pwd)
OUT=${OUT:-$T}
mkdir -p "$OUT"
FLAGS=(-std=c++20 -O0 -g -Wall -Wextra -I"$ROOT/src")
g++ "${FLAGS[@]}" "$T/market_price_test.cpp" "$ROOT/src/PlayerbotsPlusMarketPrice.cpp" -o "$OUT/market_price_test"
g++ "${FLAGS[@]}" "$T/text_test.cpp" "$ROOT/src/PlayerbotsPlusText.cpp" -o "$OUT/text_test"
g++ "${FLAGS[@]}" "$T/ledger_test.cpp" "$ROOT/src/PlayerbotsPlusLedgerText.cpp" -o "$OUT/ledger_test"
g++ "${FLAGS[@]}" "$T/dungeons_test.cpp" "$ROOT/src/PlayerbotsPlusDungeonsText.cpp" -o "$OUT/dungeons_test"
g++ "${FLAGS[@]}" "$T/groupquests_test.cpp" "$ROOT/src/PlayerbotsPlusGroupQuestsText.cpp" -o "$OUT/groupquests_test"
g++ "${FLAGS[@]}" "$T/citylife_test.cpp" "$ROOT/src/PlayerbotsPlusCityLifeText.cpp" -o "$OUT/citylife_test"
"$OUT/market_price_test"
"$OUT/text_test" "$ROOT"
"$OUT/ledger_test"
"$OUT/dungeons_test"
"$OUT/groupquests_test"
"$OUT/citylife_test"
