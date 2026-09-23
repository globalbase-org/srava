#!/bin/sh
# ★★ #3530: **segs (分割数) と n (辺数) の規約を op x カーネルで数える**。
#
#   $1 = srava 実行体
#   $2 = モジュール名 (.so)
#   $3 = segs を取る op の CSV     (このカーネルが持つものだけ)
#   $4 = n を取る op の CSV        (ngon / prism / pyramid のうち持つもの・空可)
#   $5 = segs の扱い: 1 = 実際に使う / 0 = 受けるが無視する / **none = そもそも取らない**
#        ★ #3570 段3: occt は none になった。「必要のない引数は撤去する」という原則に
#          変わったので、#3530 の「受けるが無視し検査はする」(0) は occt から消えた。
#          ⇒ none では **渡すと明示エラーになること**を見る (省略形は従来どおり値を返す)。
#   $6 = 末尾に足す引数 (省略可)。openvdb は voxel size が必須なので ",0.05" を渡す
#
# ---- なぜ「数える」形にするか ----
# #3530 の本体は「同じ `if ( segs < 3 )` を実装ごとに逆の意味で読んでいた」こと。起票時に
# 数えたら **4 通りの規約が併存**していた (sphere 族 / revolve / ngon 族 / circle が cgal と
# manifold で別)。⚠ これは **op を 1 つずつ見ても見えない** — 表にして初めて出た。
# ⇒ 表を検査にする。6 通り (省略 / 0 / 1 / 2 / -1 / 3) x 全 op x 全カーネル。
#
# ---- 固定する規約 (src/h/common/segs.h と同じ。ここが唯一の外から見える契約) ----
#   segs (近似の細かさ・既定値あり)   省略 と 0 は **同じ** / 1,2,負 は **明示エラー** / 3 以上 は その値
#   n    (形そのもの・既定値なし)     3 未満 (0 を含む) は **明示エラー**
SRAVA="$1"
SO="${2:?module .so not given}"
SEGS_OPS="$3"
N_OPS="$4"
HONORS="${5:-1}"
TAIL="${6:-}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

n=0
fail=0

# 1 式を評価して "OK <値>" か "ERR <1 行目>" を返す
ev() {
	rm -rf "$D-x"
	o=$(SRAVA_CACHE_DIR="$D-x" SRAVA_SOURCE="module(\"$SO\",{priority:99});
print(\"VAL\", $1);" "$SRAVA" 2>&1)
	v=$(echo "$o" | sed -n 's/^VAL //p' | head -1)
	if [ -n "$v" ]; then echo "OK $v"
	else echo "ERR $(echo "$o" | grep -i "must be" | head -1)"; fi
}

# ★ エラー文言も検査する。文言がずれると「同じ誤りに別の説明」が付く (共通化の意味が消える)
want_err() { # $1=ラベル $2=式 $3=期待する文言の一部
	r=$(ev "$2")
	case "$r" in
		ERR*) ;;
		*) echo "FAIL: $1 はエラーになるべきだが値を返した ($r)"; fail=1; return;;
	esac
	case "$r" in
		*"$3"*) ;;
		*) echo "FAIL: $1 の文言が違う: $r (期待: …$3…)"; fail=1; return;;
	esac
	n=$((n+1))
}
want_val() { # $1=ラベル $2=式  -> 値を stdout へ
	r=$(ev "$2")
	case "$r" in
		OK*) n=$((n+1)); echo "${r#OK }";;
		*) echo "FAIL: $1 は値を返すべきだがエラー ($r)" >&2; fail=1; echo "";;
	esac
}
same() { # $1=ラベル $2 $3
	[ -n "$2" ] && [ "$2" = "$3" ] || { echo "FAIL: $1 ($2 と $3)"; fail=1; return; }
	n=$((n+1))
}
differ() {
	[ -n "$2" ] && [ -n "$3" ] && [ "$2" != "$3" ] || { echo "FAIL: $1 ($2 と $3)"; fail=1; return; }
	n=$((n+1))
}

# op → 測り方と引数。★ ここが「列挙」の本体。新しい segs op はここへ 1 行足す
#   (足し忘れは srava_segs_coverage が名指しする)
expr_for() { # $1=op $2=segs 部分 (空 or ",0" など)
	case "$1" in
	circle)   echo "area(circle(1$2$TAIL))";;
	sphere)   echo "volume(sphere(1$2$TAIL))";;
	cylinder) echo "volume(cylinder(1,1$2$TAIL))";;
	cone)     echo "volume(cone(1,1$2$TAIL))";;
	torus)    echo "volume(torus(2,1$2$TAIL))";;
	# ★ #3555 段4: 折れ線まわりの掃引管は **occt だけ名前が違う**。occt の tube は
	#   B-spline の背骨 + 厳密な円なので、他カーネルの折れ線掃引を tube_ruled へ改名した。
	tube)       echo "volume(tube([[[0,0,0],1],[[0,0,2],1]]$2$TAIL))";;
	tube_ruled) echo "volume(tube_ruled([[[0,0,0],1],[[0,0,2],1]]$2$TAIL))";;
	revolve)  echo "volume(revolve(translate(circle(0.3),[2,0,0]),360$2$TAIL))";;
	*) echo "";;
	esac
}
nexpr_for() { # $1=op $2=n
	case "$1" in
	ngon)     echo "area(ngon($2,1)$TAIL)";;
	prism)    echo "volume(prism($2,1,1$TAIL))";;
	pyramid)  echo "volume(pyramid($2,1,1$TAIL))";;
	*) echo "";;
	esac
}

echo "---- segs を取る op: $SEGS_OPS ----"
for op in $(echo "$SEGS_OPS" | tr ',' ' '); do
	e=$(expr_for "$op" "")
	[ -n "$e" ] || { echo "FAIL: $op は srava_segs.sh の表に無い (expr_for へ 1 行足すこと)"; fail=1; continue; }

	# ★★ #3570 段3: 「そもそも segs を取らない」カーネル (occt) は、**渡すとエラー**・
	#   省略すると値、を見る。⚠ 文言は 2 通りありうる —
	#     個数で外れる      … "too many arguments"          (sphere / circle / cylinder / …)
	#     マッチ関数で外れる … "no module can execute op"     (tube: 第 2 引数はハッシュのみ)
	#   どちらも「その引数はこのカーネルに無い」を意味するので両方を受ける。
	if [ "$HONORS" = "none" ]; then
		vo=$(want_val "$op(…) 省略" "$(expr_for "$op" "")")
		for bad in 0 3 32; do
			e=$(ev "$(expr_for "$op" ",$bad")")
			case "$e" in
			ERR*) n=$((n+1)) ;;
			*) echo "FAIL: $op(…,$bad) が通ってしまう (このカーネルは segs を取らないはず): $e"; fail=1 ;;
			esac
		done
		continue
	fi

	v0=$(want_val "$op(…,0)" "$(expr_for "$op" ",0")")

	# ★ 省略 = 0 であること。⚠ TAIL がある構成 (openvdb) は末尾引数が必須なので
	#   segs を **位置的に** 省略できない ⇒ その比較は行わない (意味は同じ「既定値」)。
	if [ -z "$TAIL" ]; then
		vo=$(want_val "$op(…) 省略" "$(expr_for "$op" "")")
		same "$op: 省略 と 0 が同じ値であること" "$vo" "$v0"
	fi

	# ★★ 1 / 2 / 負 は明示エラー。ここが #3530 の本体 (以前は黙って 3 や 32 に化けていた)
	for bad in 1 2 -1; do
		want_err "$op(…,$bad)" "$(expr_for "$op" ",$bad")" "segments must be >= 3"
	done

	v3=$(want_val "$op(…,3)" "$(expr_for "$op" ",3")")
	if [ "$HONORS" = "1" ]; then
		differ "$op: segs=3 は既定と違う形になること" "$v3" "$v0"
	else
		# occt は segs を無視する。★ それでも **検査はする** (上の 3 件) —
		#   しないと同じ式が cg/mf で落ちて occt で通る、という新しい食い違いになる
		same "$op: segs を無視するカーネルでは 3 でも既定と同じ形" "$v3" "$v0"
	fi
done

if [ -n "$N_OPS" ]; then
	echo "---- n を取る op (形そのもの = 既定値なし): $N_OPS ----"
	for op in $(echo "$N_OPS" | tr ',' ' '); do
		e=$(nexpr_for "$op" 3)
		[ -n "$e" ] || { echo "FAIL: $op は srava_segs.sh の表に無い"; fail=1; continue; }
		# ★ 0 も **エラー**。segs と違い n には既定値が無い (形そのものなので)
		for bad in 0 1 2 -1; do
			want_err "$op($bad,…)" "$(nexpr_for "$op" "$bad")" "n must be >= 3"
		done
		want_val "$op(3,…)" "$(nexpr_for "$op" 3)" > /dev/null
	done
fi

rm -rf "$D-x"
[ "$fail" = "0" ] || { echo "FAIL: 上記のとおり"; exit 1; }
echo "SEGS-OK $n checks ($SO)"
