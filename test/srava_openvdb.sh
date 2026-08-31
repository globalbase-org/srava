#!/bin/sh
# openvdb (ボリューム型) モジュール (#3434 P2) の振る舞い回帰。
# $1 = srava 実行体。$2 = モード。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# モードが見ているもの:
#   inputs   … voxelize が **3 つのメッシュカーネル全部**から同じ結果を出す。
#              vd は出入り口の型として **mf-mesh3d をそのまま名乗る** (新しい型を作ると
#              他の全モジュールがそれを理解しないといけなくなるため) ので、
#              mf/gg の "MFM3" は自型としてそのまま、cg の "MESH" (厳密有理数) は
#              common/exact_wire.h で double 化して、どれも 1 本の reader で受かる。
#              ★cg 経由が bit 一致する = 厳密→double の往復が無損失であることの検査でもある。
#   converge … ★ボリューム表現の性質: **解像度が全て**。dx を細かくすると真値へ寄る。
#              「位相の場合分けが無い代わりに解像度誤差を払う」という P2 の主張の実データ。
#              ⚠ **収束は単調ではない**。粗い側は**過大評価**で、途中で**符号が変わって**
#                過小評価に転じ、そこから真値へ寄る。よって「隣り合う 2 点で細かい方が近い」は
#                成立しない (最初にそう書いて落ちた)。離れた 2 点で見ること。
#   bools    … ブール = 点ごとの min/max。★**ブールの結果は真の符号付き距離場ではなくなる**
#              (|grad| = 1 が崩れる)。形は正しいのに、|grad| = 1 を仮定する levelSetVolume が
#              **偏る**。★**測り方の問題であって、形の問題ではない**。当初「切断面が 1 ボクセル
#                太る表現の性質」と書いたが誤りで、等値面を取り出して測ると正しく、
#                renormalize すれば levelSetVolume も単体の箱と同じ水準に戻る。
#              ★2026-08-19 以降、volume() は印を見て**自動で**作り直すので、上の表の
#                「そのまま」列の値はもう出ない (renorm モードがそれを固定する)。
#              ★非対称の理由: intersection = max(a,b) は場を崩さない (同じ形を直接 voxelize
#                したものと **相対 1e-14 で一致**する) が、difference = max(a,-b) の**反転**が崩す。
#                (bit 一致ではない — 先頭 15 桁が合うだけ。最初 bit 一致と書いてテストに捕まった。)
#   roundtrip… voxelize -> isosurface -> cast("mf-mesh3d") でメッシュ系へ戻る一周。
#              ★出力型が既存の mf-mesh3d なので、下流は**何も足さずに**受け取れる。
#   dxmix    … ★格子が違うブールは**黙って計算せずエラーにする** (フォールバックはバグのもと)。
#   offset   … 距離場の等値面を動かす。d>0 で膨張・d<0 で収縮 (srava の規約)。
#              ★メッシュ系の 3D offset (nef の球との Minkowski 和) との比較は
#              docs/srava_module_reference.md の測定表を参照 — 入力メッシュの複雑さに対して
#              nef は超線形・vd は**平坦**という「形の違い」が要点。
#   renorm   … ★volume() が「場が真の距離場か」の印を見て、必要なときだけ作り直してから
#              測ること。印は grid メタデータなので .vdb キャッシュを越える = cold と warm で
#              同じ値になる。ここが壊れると warm だけ 4.2 に化ける。
SRAVA="$1"
MODE="${2:-inputs}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

vox() {   # $1=先に載せるモジュール指定 $2=dx $3=cachedir  → "体積 ボクセル数"
	rm -rf "$3"
	SRAVA_CACHE_DIR="$3" SRAVA_SOURCE="$1
	  var v = voxelize(box(2,2,2), $2);
	  print(\"R\", volume(v), voxels(v));" "$SRAVA" 2>&1 | sed -n 's/^R //p'
}

case "$MODE" in
inputs)
	MF=$(vox 'module("manifold.so",{priority:99}); module("openvdb.so",{}); module("openvdb_mf.so",{});' 0.05 "$D-mf")
	CG=$(vox 'module("cgal.so",{priority:99}); module("openvdb.so",{}); module("openvdb_cg.so",{});'     0.05 "$D-cg")
	GG=$(vox 'module("geogram.so",{priority:99}); module("openvdb.so",{}); module("openvdb_gg.so",{});'  0.05 "$D-gg")
	[ -n "$MF" ] || { echo "FAIL: manifold 入力で値が出ない"; exit 1; }
	[ -n "$CG" ] || { echo "FAIL: cgal 入力で値が出ない (MESH の昇格読みが壊れている疑い)"; exit 1; }
	# ★ 厳密一致を要求する: box は共通生成器で座標が同じ double なので、cg の厳密有理数を
	#   経由しても往復は無損失。ずれたら exact_wire.h のパーサか framing が壊れている。
	[ "$MF" = "$CG" ] || { echo "FAIL: cgal 経由 [$CG] が manifold 経由 [$MF] と違う"; exit 1; }
	if [ -n "$GG" ]; then
		[ "$MF" = "$GG" ] || { echo "FAIL: geogram 経由 [$GG] が manifold 経由 [$MF] と違う"; exit 1; }
	fi
	echo "OPENVDB-INPUTS-OK [$MF]" ;;
converge)
	# ★ 符号が変わる区間 (dx 0.1-0.05) を跨がないよう、**離れた 2 点**で見る。
	#   粗い側と細かい側で絶対誤差が桁で改善する。
	C=$(vox 'module("manifold.so",{priority:99}); module("openvdb.so",{}); module("openvdb_mf.so",{});' 0.2  "$D-c1" | cut -d' ' -f1)
	F=$(vox 'module("manifold.so",{priority:99}); module("openvdb.so",{}); module("openvdb_mf.so",{});' 0.05 "$D-c2" | cut -d' ' -f1)
	if [ -z "$C" ] || [ -z "$F" ]; then echo "FAIL: 値が出ない coarse=$C fine=$F"; exit 1; fi
	ok=$(awk -v c="$C" -v f="$F" 'BEGIN{
		dc=c-8; if(dc<0)dc=-dc; df=f-8; if(df<0)df=-df;
		# (1) 粗い側も 5% 以内には居る (2) 細かい側は 1% 以内 (3) 誤差が 3 倍以上改善する
		print (dc<0.4 && df<0.08 && df*3 < dc) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 収束していない dx=0.2 -> $C / dx=0.05 -> $F (真値 8)"; exit 1; }
	echo "OPENVDB-CONVERGE-OK dx0.2=$C dx0.05=$F" ;;
bools)
	DX=0.05
	rm -rf "$D-b"
	OUT=$(SRAVA_CACHE_DIR="$D-b" SRAVA_SOURCE="module(\"manifold.so\",{priority:99}); module(\"openvdb.so\",{}); module(\"openvdb_mf.so\",{});
	  var a = voxelize(box(2,2,2), $DX);
	  var b = voxelize(translate(box(2,2,2),[1,0,0]), $DX);
	  var direct = voxelize(translate(box(1,2,2),[0.5,0,0]), $DX);
	  print(\"U\", volume(union(a,b)));
	  print(\"I\", volume(intersection(a,b)));
	  print(\"F\", volume(difference(a,b)));
	  print(\"SI\", volume(cast(\"mf-mesh3d\", isosurface(intersection(a,b),0))));
	  print(\"SP\", volume(cast(\"mf-mesh3d\", isosurface(direct,0))));" "$SRAVA" 2>&1)
	U=$(echo "$OUT"  | sed -n 's/^U //p')
	I=$(echo "$OUT"  | sed -n 's/^I //p')
	F=$(echo "$OUT"  | sed -n 's/^F //p')
	SI=$(echo "$OUT" | sed -n 's/^SI //p')
	SP=$(echo "$OUT" | sed -n 's/^SP //p')
	if [ -z "$U" ] || [ -z "$I" ] || [ -z "$F" ] || [ -z "$SI" ] || [ -z "$SP" ]; then
		echo "FAIL: 値が出ない U=$U I=$I F=$F SI=$SI SP=$SP"; echo "$OUT"; exit 1; fi
	# 厳密値は union=12 / intersection=4 / difference=4。★difference も **volume() が自動で
	# 作り直す**ので 1% 以内に入る (印を付け忘れると大きく外れて落ちる)。
	ok=$(awk -v u="$U" -v i="$I" -v f="$F" 'BEGIN{
		du=u-12; if(du<0)du=-du; di=i-4; if(di<0)di=-di; df=f-4; if(df<0)df=-df;
		print (du<0.12 && di<0.04 && df<0.04) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: union=$U (期待 12) intersection=$I (期待 4) difference=$F (期待 4)"; exit 1; }
	# ★ intersection が作る **形**が「同じ形を直接 voxelize したもの」と一致すること。
	#   等値面から測る = **測り方 (正規化の有無) に依存しない**比較なので、volume() の方針を
	#   変えてもこの不変条件は動かない。intersection 側のずれは **桁で小さく**、difference 側は
	#   **桁で大きい**ので、1e-6 なら余裕をもって切り分けられる。
	ok=$(awk -v i="$SI" -v p="$SP" 'BEGIN{ d=i-p; if(d<0)d=-d; s=(p<0?-p:p); if(s<1)s=1;
		print (d <= 1e-6*s) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: intersection の形 [$SI] が直接 voxelize [$SP] と一致しない"; exit 1; }
	echo "OPENVDB-BOOLS-OK U=$U I=$I F=$F (iso: $SI = $SP)" ;;
roundtrip)
	rm -rf "$D-r"
	R=$(SRAVA_CACHE_DIR="$D-r" SRAVA_SOURCE="module(\"manifold.so\",{priority:99}); module(\"openvdb.so\",{}); module(\"openvdb_mf.so\",{});
	  var s = isosurface(voxelize(box(2,2,2), 0.05), 0);
	  print(\"R\", volume(cast(\"mf-mesh3d\", s)));" "$SRAVA" 2>&1 | sed -n 's/^R //p')
	[ -n "$R" ] || { echo "FAIL: 一周して値が出ない"; exit 1; }
	# ★等値面を抽出して閉じたメッシュに戻れば、体積は真値 8 のごく近くになる
	#   (面が実際に囲む体積そのものなので、level set 上の levelSetVolume より近い)。
	ok=$(awk -v r="$R" 'BEGIN{ d=r-8; if(d<0)d=-d; print (d<0.01) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 一周後の体積が $R (期待 8+-0.01)"; exit 1; }
	echo "OPENVDB-ROUNDTRIP-OK $R" ;;
dxmix)
	# ★ 格子が違う 2 つを黙って計算しない (黙って resample するフォールバックは入れない)。
	rm -rf "$D-x"
	OUT=$(SRAVA_CACHE_DIR="$D-x" SRAVA_SOURCE="module(\"manifold.so\",{priority:99}); module(\"openvdb.so\",{}); module(\"openvdb_mf.so\",{});
	  var a = voxelize(box(2,2,2), 0.05);
	  var b = voxelize(box(1,1,1), 0.1);
	  print(\"V\", volume(union(a,b)));" "$SRAVA" 2>&1)
	if echo "$OUT" | grep -q "^V "; then
		echo "FAIL: 格子が違うのに値を返した: $(echo "$OUT" | sed -n 's/^V //p')"; exit 1; fi
	echo "$OUT" | grep -q "voxel sizes differ" || {
		echo "FAIL: 期待したエラー (voxel sizes differ) が出ない"; echo "$OUT" | head -5; exit 1; }
	echo "OPENVDB-DXMIX-OK (格子違いは明示エラー)" ;;
renorm)
	# ★ ブールの結果は真の距離場ではないので levelSetVolume が偏る。
	#   volume() は **印を見て必要なときだけ作り直してから**測る (ひさ判断 2026-08-19: 黙って
	#   5% 間違う方が危険)。ここで固定するのは 2 つ:
	#     (1) ブール結果の volume が厳密値の 1% 以内 (= 自動の作り直しが効いている)
	#     (2) ★**cold と warm で同じ値**。印は grid のメタデータに載せて .vdb を越えさせて
	#         いるので、これが壊れると warm だけ 4.2 に化ける = 「答えが正しく見えたまま
	#         変わる」型の欠陥 (型スタンプで一度潰したのと同じ形の罠)。
	rm -rf "$D-n"
	PROG='module("manifold.so",{priority:99}); module("openvdb.so",{}); module("openvdb_mf.so",{});
	  var a = voxelize(box(2,2,2), 0.05);
	  var b = voxelize(translate(box(2,2,2),[1,0,0]), 0.05);'
	# cold: difference を作るが volume は計算せず cache に残す
	SRAVA_CACHE_DIR="$D-n" SRAVA_SOURCE="$PROG print(\"N\", voxels(difference(a,b)));" \
	  "$SRAVA" >/dev/null 2>&1
	# warm: 同じ cache dir で volume を要求 (difference は HIT)
	W=$(SRAVA_CACHE_DIR="$D-n" SRAVA_SOURCE="$PROG print(\"V\", volume(difference(a,b)));" \
	  "$SRAVA" 2>&1 | sed -n 's/^V //p')
	# cold 参照: 最初から volume を計算
	rm -rf "$D-n2"
	C=$(SRAVA_CACHE_DIR="$D-n2" SRAVA_SOURCE="$PROG print(\"V\", volume(difference(a,b)));" \
	  "$SRAVA" 2>&1 | sed -n 's/^V //p')
	if [ -z "$W" ] || [ -z "$C" ]; then echo "FAIL: 値が出ない warm=$W cold=$C"; exit 1; fi
	[ "$W" = "$C" ] || { echo "FAIL: warm=$W が cold=$C と違う (印が .vdb を越えていない)"; exit 1; }
	ok=$(awk -v v="$C" 'BEGIN{ d=v-4; if(d<0)d=-d; print (d<0.04) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: difference の volume が $C (期待 4±1%・印が付いていない疑い)"; exit 1; }
	echo "OPENVDB-RENORM-OK cold=warm=$C" ;;
offset)
	# 球 (半径 1・多面体近似) を +-0.2 オフセットする。厳密な球なら r=1.2 -> 7.2382 /
	# r=0.8 -> 2.1447 だが、入力が多面体なのでやや小さい値に収束する。
	rm -rf "$D-o"
	OUT=$(SRAVA_CACHE_DIR="$D-o" SRAVA_SOURCE="module(\"manifold.so\",{priority:99}); module(\"openvdb.so\",{}); module(\"openvdb_mf.so\",{});
	  var v = voxelize(sphere(1,64), 0.02);
	  print(\"O\", volume(v));
	  print(\"P\", volume(offset(v,  0.2)));
	  print(\"M\", volume(offset(v, -0.2)));" "$SRAVA" 2>&1)
	O=$(echo "$OUT" | sed -n 's/^O //p')
	P=$(echo "$OUT" | sed -n 's/^P //p')
	M=$(echo "$OUT" | sed -n 's/^M //p')
	if [ -z "$O" ] || [ -z "$P" ] || [ -z "$M" ]; then
		echo "FAIL: 値が出ない O=$O P=$P M=$M"; echo "$OUT"; exit 1; fi
	# (1) 膨張 > 元 > 収縮 (符号の向き。反転していたらここで落ちる)
	ok=$(awk -v o="$O" -v p="$P" -v m="$M" 'BEGIN{ print (p>o && o>m) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 符号が逆? 収縮=$M 元=$O 膨張=$P"; exit 1; }
	# (2) 大きさが半径 1.2 / 0.8 の球のそれに近い (多面体近似なので 5% 許容)
	ok=$(awk -v p="$P" -v m="$M" 'BEGIN{
		dp=(p-7.238229)/7.238229; if(dp<0)dp=-dp;
		dm=(m-2.144661)/2.144661; if(dm<0)dm=-dm;
		print (dp<0.05 && dm<0.05) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 膨張=$P (期待 ~7.24) 収縮=$M (期待 ~2.14)"; exit 1; }
	echo "OPENVDB-OFFSET-OK -0.2=$M  0=$O  +0.2=$P" ;;
steiner)
	# ★ offset の結果を **真値**に対して測る (#3440 の offset 厳密評価・2026-08-20)。
	#   凸多面体の Minkowski 和は Steiner の公式で厳密に出せる:
	#       V(P (+) B_d) = V + A*d + M*d^2 + (4/3)*pi*d^3
	#   box(2,2,2) は V=8 / A=24 / M=6pi なので d=0.1 の真値は 10.592684349420175。
	#
	#   ★ ここで固定したいのは値そのものより **dx を細かくすると誤差が減ること (収束)**。
	#     offset の結果に「距離場として正規化済み」の印を誤って立てると、volume() が
	#     測る前の作り直しを省き、**dx を細かくしても誤差が張り付いて減らなくなる**
	#     (2026-08-20 に実際にそうなっていた)。値の絶対誤差だけを見る従来のテスト
	#     (offset モードの 5% 許容) では、この「収束しない誤差」を検出できない。
	rm -rf "$D-st"
	OUT=$(SRAVA_CACHE_DIR="$D-st" SRAVA_SOURCE="module(\"manifold.so\",{priority:99}); module(\"openvdb.so\",{}); module(\"openvdb_mf.so\",{});
	  print(\"C\", volume(offset(voxelize(box(2,2,2), 0.05),   0.1)));
	  print(\"F\", volume(offset(voxelize(box(2,2,2), 0.0125), 0.1)));" "$SRAVA" 2>&1)
	C=$(echo "$OUT" | sed -n 's/^C //p')
	F=$(echo "$OUT" | sed -n 's/^F //p')
	if [ -z "$C" ] || [ -z "$F" ]; then echo "FAIL: 値が出ない C=$C F=$F"; echo "$OUT"; exit 1; fi
	ok=$(awk -v c="$C" -v f="$F" 'BEGIN{
		t = 10.592684349420175;
		ec = (c-t)/t; if(ec<0) ec=-ec;
		ef = (f-t)/t; if(ef<0) ef=-ef;
		# (1) 粗い側でも 1% 以内 (2) 細かい側は 0.1% 以内
		# (3) ★誤差が 2 倍以上改善する = 収束している (張り付いていない)
		print (ec<0.01 && ef<0.001 && ef*2 < ec) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: Steiner の真値 10.5926843494 に収束していない dx=0.05 -> $C / dx=0.0125 -> $F"; exit 1; }
	echo "OPENVDB-STEINER-OK dx0.05=$C dx0.0125=$F (真値 10.5926843494)" ;;
*)
	echo "unknown mode: $MODE"; exit 1 ;;
esac
