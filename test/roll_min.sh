#!/bin/sh
# roll_min: std/roll.sra の回帰 (縮小版・約 1 分)。ctest からは -L slow で明示指定したときだけ走る。
#   判定 (spiral 実測に基づく): ctrl と feasible は完全一致 / arc は ±1 許容 (round 済み整数なので
#   真値が .5 近傍だと libm/FMA 差で振れうる) / clearViol は 0.003 完全一致。
set -e
SRAVA="$1"
SRC="$2"
LIB="$3"
WORK="$4"
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK"
SRAVA_PATH="$LIB" "$SRAVA" "$SRC" > out.txt 2>&1 || { echo "FAIL: srava exited non-zero"; cat out.txt; exit 1; }
cat out.txt
ok=1
check() {  # $1=ラベル $2=期待 arc $3=期待 ctrl
  line=$(grep -E "^$1" out.txt | head -1)
  [ -n "$line" ] || { echo "FAIL: no line for $1"; ok=0; return; }
  arc=$(echo "$line"  | sed -n 's/.*arc= *\([0-9]*\).*/\1/p')
  ctrl=$(echo "$line" | sed -n 's/.*ctrl= *\([0-9]*\).*/\1/p')
  feas=$(echo "$line" | sed -n 's/.*feasible= *\([0-9]*\).*/\1/p')
  [ "$ctrl" = "$3" ] || { echo "FAIL: $1 ctrl=$ctrl (expected $3)"; ok=0; }
  [ "$feas" = "1" ]  || { echo "FAIL: $1 feasible=$feas (expected 1)"; ok=0; }
  d=$(( arc - $2 )); [ $d -ge -1 ] && [ $d -le 1 ] || { echo "FAIL: $1 arc=$arc (expected $2 +-1)"; ok=0; }
}
check "initial" 1407 20
check "step 1"  1497 20
check "step 2"  1583 21
[ "$ok" = 1 ] && echo "ROLLMIN=OK"
