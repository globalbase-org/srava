#!/bin/sh
# estimate_normals(p[,k]) の回帰 (#3528)。points.so (型) + 推定の実装 2 本。
#
# $1 = srava 実行体 / $2 = "geogram" なら geogram 版も検定する（空なら cgal 版だけ）
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- ★★ estimate_normals は「1 つの op・実装は 2 つ」------------------------------
#   cgal      pca_estimate_normals (向きなし) + mst_orient_normals (向き付け)
#   geogram   Co3Ne_compute_normals(M, k, reorient=true)
# 利用者から見れば op 名は 1 つで、実装がモジュールごとにあるのは srava では普通の形
# (nverts は 8 モジュール・minkowski は manifold と nef 系が同じ入力型で重なっている)。
# 両方ロードしていれば **priority で cgal (20 > 6)** が受け、`"geogram"::estimate_normals(p)` で
# 名指しできる (#3467)。⇒ cgal (GPL) を入れない構成でも法線推定ができる。
#
# ---- なぜ **球** なのか --------------------------------------------------------------
# ★ 法線が閉形式で書ける唯一に近い形。球面上の点 p の法線は **半径方向** (p/|p|) なので、
#   推定結果との内積が ±1 になることを直接確かめられる。しかも
#     ・|n·r̂| ≈ 1   → **推定そのもの** (接平面の当て方) が正しい
#     ・符号が全点で揃う → **向き付け** が効いている
#   の 2 つが 1 つの量で分かれて見える。⇒ 型の 2 つの印にそのまま対応する。
# ★ 点は Fibonacci 格子で作る (球面上にほぼ等間隔)。乱数を使わないので毎回同じ入力になる。
#
# ⚠⚠ **点を捨てていないこと**も見る。CGAL の作法は mst_orient_normals が向き付けられなかった
#   点を erase することだが、それをすると **黙って点が減る**。ここは点を残して印だけ下ろす実装なので、
#   nverts が入力と一致しなければならない。
#
# ⚠ 「向き付けの印」そのもの (キャッシュに載る oriented フラグ) は、まだ **消費する op が無い**ので
#   ここでは検定できない。値としての向きが揃っていることまでを見る。印の検定は法線を要求する
#   最初の op (#3515 の Poisson = 向きが無ければ明示エラー) が入った時点で入る。
LC_ALL=C
export LC_ALL
SRAVA="$1"
HAS_GG="$2"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
W="$D-work"
# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
. "$(dirname "$0")/srava_filecmp.sh"

rm -rf "$W"
mkdir -p "$W" || exit 1

MOD='module("points.so",{}); module("cgal.so",{});'
if [ "$HAS_GG" = "geogram" ]; then
	MOD="$MOD module(\"geogram.so\",{});"
fi

N=800
R=2
SPH="$W/sphere.xyz"
awk -v n=$N -v r=$R 'BEGIN{
	pi = 3.14159265358979; ga = pi * (3 - sqrt(5));   # 黄金角
	for ( i = 0 ; i < n ; ++i ) {
		z = 1 - 2*(i+0.5)/n; s = sqrt(1 - z*z); t = ga*i;
		printf "%.9f %.9f %.9f\n", r*s*cos(t), r*s*sin(t), r*z;
	}
}' > "$SPH"

# check <ラベル> <キャッシュ名> <op の書き方 ("" / "\"cgal\"::" / "\"geogram\"::")> <出力ファイル>
check() {
	rm -rf "$D-$2"
	O=$(SRAVA_CACHE_DIR="$D-$2" SRAVA_SOURCE="$MOD
var q = $3estimate_normals(import(\"$SPH\"));
print(\"V\", nverts(q));
export(\"$4\", q);" "$SRAVA" 2>&1)
	GOT=$(echo "$O" | sed -n 's/^V //p')
	[ "$GOT" = "$N" ] || { echo "EN_FAIL($1): nverts が $GOT (期待 $N・点を捨てていないこと)"; echo "$O"; exit 1; }
	NF1=$(awk 'NR==1{print NF}' "$4")
	[ "$NF1" = "6" ] || { echo "EN_FAIL($1): 出力が $NF1 列 (期待 6 = 法線ありの印が export まで届く)"; exit 1; }
	awk -v tag="$1" '
	NR==1 { mnl=9; mxl=-9; mnd=9; mxd=-9 }
	{
		l  = sqrt($4*$4 + $5*$5 + $6*$6);            # 法線の長さ
		rr = sqrt($1*$1 + $2*$2 + $3*$3);            # 原点からの距離
		d  = ($1*$4 + $2*$5 + $3*$6) / rr;           # n · r̂
		if ( l < mnl ) mnl = l;  if ( l > mxl ) mxl = l;
		if ( d < mnd ) mnd = d;  if ( d > mxd ) mxd = d;
	}
	END {
		printf "EN_RANGE(%s): |n| = %.6f .. %.6f / n·rhat = %.6f .. %.6f (%d 点)\n",
		       tag, mnl, mxl, mnd, mxd, NR;
		if ( mnl < 0.999 || mxl > 1.001 ) { printf "EN_FAIL(%s): 法線が単位ベクトルでない\n", tag; exit 1 }
		# 半径方向かつ符号が揃う = 全点で同符号の ±1。範囲が 0 をまたいだら向き付けが失敗している。
		if ( mnd > 0.99 && mxd <= 1.001 )   { printf "EN_ORIENT(%s): 全点 外向き\n", tag; exit 0 }
		if ( mxd < -0.99 && mnd >= -1.001 ) { printf "EN_ORIENT(%s): 全点 内向き\n", tag; exit 0 }
		printf "EN_FAIL(%s): 法線が半径方向でない/向きが揃っていない\n", tag;
		exit 1
	}' "$4" || exit 1
}

check "既定"   d  ""              "$W/def.xyz"
check "cgal"   cg '"cgal"::'      "$W/cg.xyz"

# ★ 既定 (両方ロード) は **priority で cgal** に解決される (cgal 20 > geogram 6)。
file_same "$W/def.xyz" "$W/cg.xyz"; _rc=$?
if [ $_rc -ge 2 ]; then echo "EN_FAIL: 既定と cgal を比較できなかった (rc=$_rc ・ 2=比較手段が無い / 3=ファイルが読めない)"; exit 1; fi
if [ $_rc -ne 0 ]; then
	echo "EN_FAIL: 既定が cgal に解決されていない (priority 20 > 6 のはず)"; exit 1
fi

if [ "$HAS_GG" = "geogram" ]; then
	check "geogram" gg '"geogram"::'   "$W/gg.xyz"
	# ⚠ 2 つの実装は **別の答え**を出す (同じ球ならどちらも半径方向だが、値は一致しない)。
	#   一致してしまったら名指しが効いていない疑いがある。
	# ⚠⚠ ここを @if file_same …; then@ と書いてはいけない — rc 2 / 3 (比較できなかった) が
	#   «違う» に化けて、**一度も比較していないのに緑になる**。上の 2 箇所と逆向きの同じ穴。
	file_same "$W/cg.xyz" "$W/gg.xyz"; _rc=$?
	if [ $_rc -eq 0 ]; then
		echo "EN_FAIL: cgal 版と geogram 版の出力が同一 (名指しが効いていない?)"; exit 1
	elif [ $_rc -ge 2 ]; then
		echo "EN_FAIL: cgal 版と geogram 版を比較できなかった (rc=$_rc)"; exit 1
	fi
	# ★★ キャッシュキーが混ざらないこと: 同じ cache dir で 2 つを続けて走らせると
	#   **2 回とも miss** になる (ソルトにモジュール名 + cache_version が入るため)。
	#   ⚠ ここが壊れると、片方の結果がもう片方の名前で返る = 静かに別の実装の答えになる。
	rm -rf "$D-mix"
	MM=$(SRAVA_CACHE_DIR="$D-mix" SRAVA_SOURCE="$MOD
print(\"V\", nverts(\"cgal\"::estimate_normals(import(\"$SPH\"))));
print(\"V\", nverts(\"geogram\"::estimate_normals(import(\"$SPH\"))));" "$SRAVA" 2>&1 |
		sed -n 's/.*cache: \([0-9]*\) hit(s), \([0-9]*\) miss(es).*/\1 \2/p')
	set -- $MM
	# import は 2 回目が hit する (同じファイル) が、estimate_normals は 2 つとも miss でなければならない。
	[ "${2:-0}" -ge 3 ] || { echo "EN_FAIL: 2 実装のキャッシュが混ざっている (hit=$1 miss=$2)"; exit 1; }
	echo "EN_CACHE: 別実装は別キー (hit=$1 miss=$2)"
fi

# ⑤ 明示エラー: 法線を推定するには点が要る (2 点以下は接平面が決まらない)
O=$(rm -rf "$D-en2"; SRAVA_CACHE_DIR="$D-en2" SRAVA_SOURCE="$MOD
print(\"V\", nverts(estimate_normals(points3d([[0,0,0],[1,0,0]]))));" "$SRAVA" 2>&1)
case "$O" in
*"at least 3 points"*) ;;
*) echo "EN_FAIL: 2 点がエラーにならない: $O"; exit 1 ;;
esac

# ⑥ 2D は受けない (法線推定は 3D だけ・points.so が pt-cloud2d を持つ)
O=$(rm -rf "$D-en3"; SRAVA_CACHE_DIR="$D-en3" SRAVA_SOURCE="$MOD
print(\"V\", nverts(estimate_normals(points2d([[0,0],[1,0],[0,1]]))));" "$SRAVA" 2>&1)
case "$O" in
*"pt-cloud2d"*) ;;
*) echo "EN_FAIL: 2D がエラーにならない: $O"; exit 1 ;;
esac

echo "EN_OK"
