#!/bin/sh
# ★★ #3529: **cache を経由しても同じ値になること** (同じ式が経路で違う値を返さない)。
#
#   $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 2D を持つか (1/0)
#
# ---- 何が壊れていたか ----
# manifold の 2D は @decode@ が @CrossSection(ps, NonZero)@ で作り直しており、構成子が
# **Clipper2 の Union を通る**ので座標が整数格子に載っていた。⇒ *その場で計算した実体* と
# *cache から decode した実体* で値が違う:
#     area(circle(1))                     3.1214451522580524   Union を通らない
#     area(translate(circle(1),[0,0,4]))  3.121445153570154    decode で Union  ← 症状
# ⚠ これは性能でなく **正しさ**の問題 — content-addressed cache は「同じ入力なら同じ結果」を
#   前提に値を再利用する仕組みなので、hit と miss で答えが変わるのは約束そのものに触る。
#
# ---- ⚠⚠ 素朴に「同じ式を 2 回流して比べる」は **検定にならない** ----
# 2 回目は **終端 op 自体が cache hit する**ので、同じ値が出るのが当たり前。必要な形はこれ:
#
#   run1 (dir D)  下位の body だけを焼く      例) nverts(...)
#   run2 (dir D)  **別の終端 op** を計算させる 例) area(...)   ← body は decode 経由
#   run3 (dir E)  まっさらで同じ終端 op        例) area(...)   ← body はその場
#   run2 == run3 を **文字列一致**で見る
#
# ★ 2D だけでなく **3D も横断**する。3D は 2026-09-14 の実測で無罪 (Manifold(MeshGL64) は
#   座標一致で頂点マージするだけで格子が無い) だが、*将来 manifold 側が変わったら赤くなる*
#   ように固定しておく価値がある。
SRAVA="$1"
SO="${2:?module .so not given}"
HAS2D="${3:-1}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

n=0
fail=0
nskip=0
skip=""

# 出力を丸ごと返す (値の取り出しと「op が無い」の判定を呼び手で分ける)
run() { # $1=cache dir $2=式
	SRAVA_CACHE_DIR="$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
print(\"VAL\", $2);" "$SRAVA" 2>&1
}
val() { echo "$1" | sed -n 's/^VAL //p' | head -1; }
# ★ そのカーネルが op / 型を持たないだけ、を **失敗と区別する**。
#   ⚠ 混ぜると「型が無い」と「本当の歯抜け」が同じ赤になり、表が嘘になる (#3510 の教訓)。
missing() { echo "$1" | grep -q "no module can execute op"; }

# $1=ラベル / $2=下位を焼く式 (別の終端) / $3=比べる式
pathcheck() {
	dir="$D-w$n"; fresh="$D-f$n"
	rm -rf "$dir" "$fresh"
	o1=$(run "$dir" "$2")                 # run1: 下位の body を焼く (終端は **別 op**)
	o2=$(run "$dir" "$3")                 # run2: 同じ dir ⇒ body は **decode 経由**
	o3=$(run "$fresh" "$3")               # run3: まっさら ⇒ body は **その場**
	rm -rf "$dir" "$fresh"
	if missing "$o1" || missing "$o3"; then
		skip="$skip $1"; nskip=$((nskip+1)); return   # そのカーネルが持たない op/型
	fi
	n=$((n+1))
	warm=$(val "$o2"); cold=$(val "$o3"); seed=$(val "$o1")
	if [ -z "$seed" ] || [ -z "$warm" ] || [ -z "$cold" ]; then
		echo "FAIL: $1 の評価が空 (seed=$seed warm=$warm cold=$cold)"
		echo "$o3" | grep -i "ERROR" | head -1
		fail=1; return
	fi
	# ★ **文字列一致**で見る。相対許容だと *まさに直したかった下位桁の差*を見逃す
	[ "$warm" = "$cold" ] || {
		echo "FAIL: $1 が cache 経路で変わった"
		echo "        decode 経由 = $warm"
		echo "        その場      = $cold"
		fail=1; }
}

if [ "$HAS2D" = "1" ]; then
	echo "---- 2D (#3529 の本体) ----"
	# ★ 種は「下位の body を焼かせる **別の終端**」でありさえすればよい。
	#   ⚠ 2D の計測 op はカーネルで歯抜けがある (実測):
	#       occt      2D に nverts を持たない     no module can execute op 'nverts' on (oc-face3d)
	#       manifold  2D に bbox   を持たない     no module can execute op 'bbox'   on (mf-cross2d)
	#       occt      2D 同士の差を持たない       no module can execute op 'difference' on (oc-face3d,oc-face3d)
	#   ⇒ 種は **2D を 3D へ持ち上げる** volume(extrude(...)) に揃える (3 カーネルとも持つ)。
	#     持っていない組み合わせは *飛ばして名前を出す* — 「型が無い」と「本当の歯抜け」を
	#     混ぜると表が嘘になる。
	# ★ いちばん小さい的。起票時の再現そのもの
	pathcheck "area(circle)"    'volume(extrude(circle(1),1))'   'area(circle(1))'
	pathcheck "area(translate)" 'volume(extrude(translate(circle(1),[3,0]),1))' 'area(translate(circle(1),[3,0]))'
	pathcheck "bbox(circle)"    'area(circle(1))'                'bbox(circle(1))'
	pathcheck "area(rect)"      'volume(extrude(rect(2,3),1))'      'area(rect(2,3))'
	# ★ 穴あき (decode の正規化が効いていた可能性がある形。2026-09-14 の留保)
	pathcheck "area(穴あき)"    'volume(extrude(circle(2) --- circle(1),1))' 'area(circle(2) --- circle(1))'
	# ★★ 表現に敏感な op — area では見えず loft_ruled で初めて出た (#3511) のが起票の経緯。
	#   2D の body を焼いてから **3D へ持ち上げる**ので、座標が動けば体積と頂点数に出る
	pathcheck "volume(extrude)"     'area(circle(1))'                   'volume(extrude(circle(1),2))'
	pathcheck "nverts(extrude)"     'area(circle(1))'                   'nverts(extrude(circle(1),2))'
fi

echo "---- 3D (無罪のはずだが固定する) ----"
pathcheck "volume(sphere)"  'nverts(sphere(1))'      'volume(sphere(1))'
pathcheck "nfaces(sphere)"  'volume(sphere(1))'      'nfaces(sphere(1))'
# ★ union を焼いてから decode し、**さらに差をとる** 形。
#   2D で area では見えず loft_ruled で出たのと同じ罠を避けるため (1 段では露出しない)
pathcheck "volume(合成)" \
	'nverts((sphere(1) ||| translate(sphere(1),[1,0,0])) --- sphere(0.5))' \
	'volume((sphere(1) ||| translate(sphere(1),[1,0,0])) --- sphere(0.5))'

[ -n "$skip" ] && echo "  note: $SO が持たない op/型のため飛ばした:$skip"
[ "$fail" = "0" ] || { echo "FAIL: 上記のとおり"; exit 1; }
echo "CACHEPATH-OK $n checks ($SO・飛ばし $nskip)"
