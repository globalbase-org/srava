#!/bin/sh
# hull (凸包・#3511) の **閉形式**回帰。カーネル合議に頼らずに値を固定する。
# $1 = srava 実行体。$2 = モジュール (.so 名)。$3 = 許容相対誤差。$4 = 追加検査 (空 / "2d" / "flat" / "2d,flat")。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# 見ているもの:
#   ① 凸なものの凸包は **それ自身** — box(2,3,4) の凸包は体積 24 のまま。
#   ② 単位箱 2 個 (原点 / x+2) の凸包 = 3x1x1 の箱 = **3 ちょうど**。
#   ③ 単位箱 3 個 (原点 / x+2 / y+3) の凸包 = 底面の 2D 凸包 (0,0)-(3,0)-(3,1)-(1,4)-(0,4)
#      = 面積 9 を厚み 1 で押し出したもの = **9 ちょうど**。
#      ★ 斜めに置くのが肝 — 軸平行に並べると bbox と区別がつかない。
#   ④ 測地球 (凸) の凸包はそれ自身 — icosphere と **同じ値**になる。
#      ★ ③ までは「凸包らしい値」でも通りうるが、④ は形を変えていないことまで言う。
#   ⑤ (2d) 単位正方形 2 枚 (原点 / x+2) の 2D 凸包 = 3x1 の長方形 = **面積 3 ちょうど**。
#      ⚠ #3533: 2 枚目を translate で作ると **face3d** になり、答えは *立体* になる (規約③)。
#        2D の凸包が欲しいときは **両方とも z=0 の簡易表現のまま**渡す。
#   ⑥ (flat) ★★ **退化を黙って通さない** — 1 平面上の点集合は凸包が立体にならない。
#      ⚠ ここは「0 が返る」でも「ハングする」でもなく **明示エラー**であることを見る。
#        実測でそれぞれ 1 件ずつ踏んだ: manifold は面 6 枚の潰れた箱を volume 0 で返し、
#        geogram (中身は 3D Delaunay) は **戻ってこなかった**。
#      ⚠ nef では検査できない — nef は hull 以前に box(1,1,0) 自体で SIGSEGV する (別件)。
SRAVA="${1:?srava binary not given}"
SO="${2:?module .so not given}"
TOL="${3:-1e-12}"
EXTRA="${4:-}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
M="module(\"$SO\",{priority:99});"
fails=0

near() {   # near <got> <want> <name>
	ok=$(awk -v g="$1" -v w="$2" -v t="$TOL" 'BEGIN{
		d = g - w; if (d < 0) d = -d;
		r = (w < 0 ? -w : w); if (r < 1) r = 1;
		print (d / r <= t) ? 1 : 0 }')
	if [ "$ok" = "1" ]; then echo "ok    $3 = $1"
	else echo "FAIL: $3 = $1 (期待 $2・許容 $TOL)"; fails=$((fails+1)); fi
}

# ★★ `timeout(1)` は GNU coreutils。**macOS には無い** (`gtimeout` も既定では入らない)。
#   ⚠ 無いまま呼ぶと "command not found" だけが $OUT に入り、⑥ は「明示エラーにならない」側へ
#     落ちる (しかも値は空)。**製品は正しいのにテストだけ赤くなる** (2026-09-12 に mac で実際に
#     踏んだ)。⇒ 使えるものを選び、どちらも無ければシェルで番犬を立てる。
#   ⚠ srava は **SIGTERM を無視する**ので、素の `timeout` では止まらない。`-k` で SIGKILL を
#     用意する / 番犬側は最初から `kill -9`。
#   ⚠ 番犬の fd は閉じること。開けたままだとコマンド置換のパイプを塞いで $(...) が終われない。
run_limited() {   # run_limited <sec> <cmd...>
	if command -v timeout > /dev/null 2>&1 ; then timeout -k 5 "$@" ; return $? ; fi
	if command -v gtimeout > /dev/null 2>&1 ; then gtimeout -k 5 "$@" ; return $? ; fi
	_sec="$1" ; shift
	"$@" &
	_pid=$!
	( _n=0
	  while [ "$_n" -lt "$_sec" ] ; do
		sleep 1
		kill -0 "$_pid" 2> /dev/null || exit 0
		_n=$((_n+1))
	  done
	  kill -9 "$_pid" 2> /dev/null ) > /dev/null 2>&1 &
	_wd=$!
	wait "$_pid" ; _rc=$?
	kill "$_wd" 2> /dev/null ; wait "$_wd" 2> /dev/null
	return "$_rc"
}

run() {   # run <suffix> <src>
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="$M $2" "$SRAVA" 2>&1
}

OUT=$(run main '
  var b = box(1,1,1);
  print("A", volume(hull(box(2,3,4))));
  print("B", volume(hull(b, translate(b,[2,0,0]))));
  print("C", volume(hull(b, translate(b,[2,0,0]), translate(b,[0,3,0]))));
  print("D", volume(hull(icosphere(5,2))));
  print("E", volume(icosphere(5,2)));')
A=$(echo "$OUT" | sed -n 's/^A //p'); B=$(echo "$OUT" | sed -n 's/^B //p')
C=$(echo "$OUT" | sed -n 's/^C //p'); Dv=$(echo "$OUT" | sed -n 's/^D //p')
E=$(echo "$OUT" | sed -n 's/^E //p')
if [ -z "$A" ] || [ -z "$B" ] || [ -z "$C" ] || [ -z "$Dv" ] || [ -z "$E" ]; then
	echo "FAIL: 値が出ない A=$A B=$B C=$C D=$Dv E=$E"; echo "$OUT"; exit 1
fi
near "$A" 24 "① 凸なものの凸包はそれ自身 (box 2x3x4)"
near "$B" 3  "② 単位箱 2 個の凸包"
near "$C" 9  "③ 単位箱 3 個 (斜め) の凸包"
near "$Dv" "$E" "④ 測地球の凸包はそれ自身"

case ",$EXTRA," in *,2d,*)
	OUT=$(run 2d 'print("F", area(hull(rect(1,1), polygon([[2,0],[3,0],[3,1],[2,1]]))));')
	F=$(echo "$OUT" | sed -n 's/^F //p')
	if [ -z "$F" ]; then echo "FAIL: 2D の値が出ない"; echo "$OUT"; fails=$((fails+1))
	else near "$F" 3 "⑤ 単位正方形 2 枚の 2D 凸包"; fi ;;
esac

case ",$EXTRA," in *,flat,*)
	# ★★ 2026-09-12: **退化した入力を srava の口から作るのが難しくなった** — #3516 で
	#   box / sphere / icosphere / prism の寸法検査と transform の特異行列検査を揃えたので、
	#   `box(1,1,0)` も `transform(...,[...det 0...])` も *hull へ届く前に* 弾かれる。
	#   ⇒ 残っている正当な経路は 2 本で、カーネルによって踏める側が違う:
	#     (a) 2D の共線な polygon … cgal / manifold (2D 型を持つ側)
	#     (b) 平らな STL の import … geogram / nef (2D 型を持たない側)
	#   ⚠ manifold に (b) を使うと **import の段階で "not manifold" が先に出る** ので、
	#     hull の退化ガード自体を踏めない。だから 2D 型の有無で経路を分ける。
	if [ "${EXTRA#*2d}" != "$EXTRA" ] ; then
		FLATSRC="$M print(\"G\", area(hull(polygon([[0,0],[1,0],[2,0]]))));"
	else
		# 平らな板 (三角形 2 枚・すべて z=0)。⚠ export では作れないので直に書く。
		cat > "$D-flat.stl" <<'STLEOF'
solid flat
facet normal 0 0 1
outer loop
vertex 0 0 0
vertex 1 0 0
vertex 1 1 0
endloop
endfacet
facet normal 0 0 1
outer loop
vertex 0 0 0
vertex 1 1 0
vertex 0 1 0
endloop
endfacet
endsolid flat
STLEOF
		FLATSRC="$M print(\"G\", volume(hull(import(\"$D-flat.stl\"))));"
	fi
	# ⚠ ハングも失敗なので時間で殺す (run_limited)。
	# ⚠ env 経由にするのは、関数呼び出しの前置代入が sh の実装で持ち越されないことがあるため。
	OUT=$(rm -rf "$D-flat"; run_limited 60 env SRAVA_CACHE_DIR="$D-flat" \
	      SRAVA_SOURCE="$FLATSRC" "$SRAVA" 2>&1)
	if echo "$OUT" | grep -q "degenerate"; then
		echo "ok    ⑥ 退化 (1 直線上 / 1 平面上) は明示エラー"
	elif [ -z "$OUT" ]; then
		echo "FAIL: ⑥ 退化した入力で戻ってこない (60 秒で kill)"; fails=$((fails+1))
	else
		echo "FAIL: ⑥ 退化した入力が明示エラーにならない: $(echo "$OUT" | sed -n 's/^G //p;/ERROR/p' | head -1)"
		fails=$((fails+1))
	fi ;;
esac

if [ "$fails" = "0" ]; then echo "HULL-OK ($SO)"; else echo "HULL-FAIL ($fails fail)"; fi
exit "$fails"
