#!/bin/sh
# ★ #3436 P4: n 項ノードの **評価時**分解の回帰。$1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# パーサが木に分解するのをやめ、n 項ノードのまま dispatch へ渡して
# pigfModuleAgent::try_decompose が k 項の木を組むようになった (docs/sig_grammar_design.md §5.5)。
# 検査するのは「木の形が変わっていないこと」= **キャッシュの共有**で見る:
#   ① union(a,b,c,d) と union([a,b,c,d]) が同じ木 (旧実装は別の木だった = §5.3 ②)
#   ② difference(a,b,c) が左 fold のまま (difference(difference(a,b),c) と同じ木)
#   ③ 単位元 {} の短絡が n 項でも効く (旧実装は 2 項専用だった)
#   ④ module(so,{arity:k}) の検証 (k は 2 以上の有限整数)
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
FAIL=0

# ★ #3452: 起動時 eager-load 撤去に伴い、box/union/difference/translate の実行に実カーネルの
#   明示ロードが要る。include "module/all.sra" の解決に要る SRAVA_PATH も併せて渡す
#   (cmake ENVIRONMENT が設定していないため)。
export SRAVA_MODULE_ALL=1
SRAVA_PATH="$(cd "$(dirname "$0")/../lib" && pwd)"
export SRAVA_PATH

# $1=ラベル $2=期待 $3=実際
eq() {
	if [ "$2" != "$3" ]; then echo "NARY_FAIL: $1 — 期待 '$2' / 実際 '$3'"; FAIL=1
	else echo "  ok $1 ($3)"; fi
}

# ソースを走らせ "hit miss value" を返す。
run() {
	OUT=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$1" "$SRAVA" 2>&1)
	H=$(echo "$OUT" | sed -n 's/.*cache: \([0-9]*\) hit(s), \([0-9]*\) miss.*/\1 \2/p')
	V=$(echo "$OUT" | sed -n 's/.*result value=\([0-9.]*\).*/\1/p')
	echo "$H $V"
}

BOXES='var a = box(2,2,2); var b = box(1,1,4); var c = box(3,1,1); var d = box(1,3,1);'

# ① n 項の呼び出し形と配列形が同じ木 → 2 回目は全 HIT。
rm -rf "$D"
R1=$(run "$BOXES print(volume(union(a,b,c,d)));")
eq "① union(a,b,c,d) cold"        "0 8 12.0" "$R1"
R2=$(run "$BOXES print(volume(union([a,b,c,d])));")
eq "① union([a,b,c,d]) が全 HIT" "8 0 12.0" "$R2"

# ② difference の左 fold が保たれている。
rm -rf "$D"
DB='var a = box(4,4,4); var b = box(1,1,9); var c = box(9,1,1);'
R3=$(run "$DB print(volume(difference(a,b,c)));")
eq "② difference(a,b,c) cold"                 "0 6 57.0" "$R3"
R4=$(run "$DB print(volume(difference(difference(a,b),c)));")
eq "② difference(difference(a,b),c) が全 HIT" "6 0 57.0" "$R4"

# ③ 単位元 {} の短絡が n 項で効く (幾何は計算されるが {} は消える)。
for t in "union(box(2,2,2),{},box(1,1,4))|10.0" \
         "union({},{},box(2,2,2))|8.0" \
         "union({},{},{})|0.0" \
         "difference({},box(1,1,1),box(2,2,2))|0.0" \
         "difference(box(4,4,4),{},box(1,1,9))|60.0" \
         "union([box(2,2,2),{},box(1,1,4)])|10.0" ; do
	E=$(echo "$t" | sed 's/|.*//'); W=$(echo "$t" | sed 's/.*|//')
	rm -rf "$D"
	G=$(run "print(volume($E));")
	eq "③ $E" "$W" "$(echo "$G" | awk '{print $3}')"
done

# ④ module(so,{arity:k}) の検証。
rm -rf "$D"
M=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE='module("cgal.so",{arity:4}); print(volume(box(2,2,2)));' "$SRAVA" 2>&1)
case "$M" in *"result value=8.0"*) echo "  ok ④ arity:4 は受理される";; *) echo "NARY_FAIL: ④ arity:4 が通らない: $M"; FAIL=1;; esac
M=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE='module("cgal.so",{arity:1});' "$SRAVA" 2>&1)
case "$M" in *"arity must be an integer >= 2"*) echo "  ok ④ arity:1 は明示エラー";; *) echo "NARY_FAIL: ④ arity:1 がエラーにならない: $M"; FAIL=1;; esac

# ⑤ ★ capability (op の sig の N) が policy (N') を頭打ちにすること。
#    cgal の corefinement は二項だけなので sig を "(2)" と申告している。arity:4 を指定しても
#    木は二項のまま = 節点数が変わらない。「N' の既定 2 に安全性を預けない」の回帰。
BOX4='var v=[]; v[0]=translate(box(2,2,2),[1,0,0]); v[1]=translate(box(2,2,2),[2,0,0]); v[2]=translate(box(2,2,2),[3,0,0]); v[3]=translate(box(2,2,2),[4,0,0]);'
rm -rf "$D"; C2=$(run "module(\"cgal.so\",{priority:99,arity:2}); $BOX4 print(volume(union(v)));")
rm -rf "$D"; C4=$(run "module(\"cgal.so\",{priority:99,arity:4}); $BOX4 print(volume(union(v)));")
eq "⑤ cgal は sig(2) なので arity:4 でも木が同じ" "$(echo "$C2" | awk '{print $2}')" "$(echo "$C4" | awk '{print $2}')"

# ⑥ ★ planner 側の引数種別検査 (§6.2)。従来この検査は agent 側にしか無く、**計算が全部
#    走ってから**落ちていた。いまはモジュールが決まった直後に落ちるので、幾何の cache が
#    1 つも完成しない。
rm -rf "$D"
M=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE='print(volume(translate([1,0,0], box(2,2,2))));' "$SRAVA" 2>&1)
case "$M" in *"argument 1 should be a mesh"*) echo "  ok ⑥ 引数種別のエラーが出る";;
             *) echo "NARY_FAIL: ⑥ 引数種別のエラーが出ない: $M"; FAIL=1;; esac
NC=$(ls "$D" 2>/dev/null | grep -c cache)
eq "⑥ エラー時に幾何 cache が完成していない" "0" "$NC"

rm -rf "$D"
[ "$FAIL" = 0 ] && echo "NARY-FOLD-OK"
