#!/bin/sh
# 退化した / 負のプリミティブ寸法が **明示エラー**になることの回帰 (#3516)。
# $1 = srava 実行体。$2 = モジュール (.so 名)。$3 = 追加の系列 (空 / "2d")。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ なぜ要るか (2026-09-12 の実測):
#   「寸法は > 0」という約束は cylinder / cone / torus / tetrahedron / pyramid では
#   7 カーネル全部が持っていたのに、**box と sphere では 2/7 しか持っていなかった**
#   (icosphere / prism も 5/7)。その歯抜けの結果:
#     ・nef   box(1,1,0) で **SIGSEGV** (CGAL の SNC 構築が厚みゼロの面で落ちる。
#             例外ではないので呼び手の try では受けられない)
#     ・cgal / manifold / geogram は「体積 0 の立体」を **黙って**作る
#     ・box(-1,1,1) は nef=1 / cgal=-1 / manifold=0 / geogram=0.99999… と
#       **同じ入力に 4 通りの違う値**が返っていた
#   ⇒ 検査そのものではなく **カーネル間で歯抜けになること**が事故の源なので、
#     ここでは「全カーネルが同じ入力を同じ理由で断る」ことを見る。
#
# ⚠ openvdb は入れない — 引数の並びが違う (末尾に dx が要る・sphere は seg でなく dx)。
#   もともと 7/7 の側だったので、歯抜けの回帰としては他の 6 つで足りる。
SRAVA="${1:?srava binary not given}"
SO="${2:?module .so not given}"
EXTRA="${3:-}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
M="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});"
fails=0
i=0

# 各行 = <式>@@<エラー文に一致する正規表現>  (区切りが @@ なのは、正規表現に | を使うため)
#   volume() で終わるもの = 3D。area() で終わるもの = 2D (2D 型を持つカーネルだけ)。
CASES='box(1,1,0)@@box: sizes must be > 0
box(1,0,1)@@box: sizes must be > 0
box(0,1,1)@@box: sizes must be > 0
box(-1,1,1)@@box: sizes must be > 0
boxa([1,1,0])@@box: sizes must be > 0
sphere(0)@@sphere: radius must be > 0
sphere(-1)@@sphere: radius must be > 0
icosphere(0,2)@@icosphere: radius must be > 0
prism(2,1,1)@@prism: n must be >= 3
prism(6,0,1)@@prism: height must be > 0
prism(6,2,0)@@prism: radius must be > 0
pyramid(2,1,1)@@pyramid: n must be >= 3
tetrahedron(0)@@tetrahedron: circumradius must be > 0
transform(box(1,1,1),[1,0,0,0, 0,1,0,0, 0,0,0,0])@@transform: singular matrix
scale(box(1,1,1),[1,1,0])@@scale: degenerate'
# ★ #3555 段4: 折れ線まわりの掃引管は **occt だけ名前が違う** (occt の tube = B-spline の
#   背骨 + 厳密な円 / 他カーネルの折れ線掃引 = tube_ruled)。検査そのものは共通ヘッダ
#   src/h/common/tube.h にあるので、名前だけ差し替えて同じ 2 件を全カーネルで回す。
case "$SO" in occt.so) TUBE=tube;; *) TUBE=tube_ruled;; esac
CASES="$CASES
$TUBE([[[0,0,0],-1],[[2,0,0],1]])@@$TUBE:.*radius
$TUBE([[[0,0,0],1]])@@$TUBE: needs >= 2 path vertices"
# ★ 2D は 2D 型を持つカーネル (cgal / manifold / occt) だけ。
CASES2D='ngon(2,1)@@ngon: n must be >= 3
ngon(6,0)@@ngon: radius must be > 0
ngon(6,-1)@@ngon: radius must be > 0
circle(0)@@circle: radius must be > 0
circle(-1)@@circle: radius must be > 0
rect(0,1)@@rect: width and height must be > 0
rect(-1,1)@@rect: width and height must be > 0
polygon([[0,0],[1,0]])@@polygon: needs >= 3 points
extrude(rect(1,1), 0)@@extrude: height must (be non-zero|not be 0)
revolve(rect(1,1), 0)@@revolve: angle must be'

run_cases() {   # run_cases <系列名> <計測関数> <ケース表>
	echo "$3" | while IFS='@' read -r EXPR SEP WANT; do
		[ -n "$EXPR" ] || continue
		i=$((i+1))
		rm -rf "$D-$1$i"
		OUT=$(SRAVA_CACHE_DIR="$D-$1$i" SRAVA_SOURCE="$M print(\"V\", $2($EXPR));" "$SRAVA" 2>&1)
		if echo "$OUT" | grep -qE "$WANT"; then
			echo "ok    $EXPR"
		else
			V=$(echo "$OUT" | sed -n 's/^V //p')
			if [ -n "$V" ]; then
				echo "FAIL: $EXPR が値 $V を返した (期待: エラー \"$WANT\")"
			elif echo "$OUT" | grep -q "SIG[A-Z]"; then
				echo "FAIL: $EXPR で **シグナルで落ちた** (期待: エラー \"$WANT\")"
			else
				echo "FAIL: $EXPR のエラー文が違う (期待: \"$WANT\"): $(echo "$OUT" | grep -o 'ERROR.*' | head -1)"
			fi
			echo "$EXPR" >> "$D.fails"
		fi
	done
}

run_cases 3d volume "$CASES"
case ",$EXTRA," in *,2d,*) run_cases 2d area "$CASES2D" ;; esac

# ⚠ while はサブシェルなので変数を持ち越せない。失敗はファイルで数える。
if [ -s "$D.fails" ]; then
	fails=$(wc -l < "$D.fails" | tr -d ' ')
	rm -f "$D.fails"
	echo "DEGEN-FAIL ($fails fail)"
	exit 1
fi
rm -f "$D.fails"
echo "DEGEN-OK ($SO)"
