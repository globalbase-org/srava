#!/bin/sh
# lemonc++ パーサの回帰テスト。$1=srava 実行体, $2=モード。SRAVA_SOURCE をここで設定する
# (セミコロンを含むので cmake の ENVIRONMENT 経由ではなくスクリプト内で渡す)。
SRAVA="$1"
MODE="$2"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★ #3452: 起動時 eager-load 撤去に伴い、この回帰群は大半が module() 未呼出のまま幾何 op を
#   使う (=旧挙動に暗黙依存)。個別に module()/include を足す代わりに、この harness 全体へ
#   SRAVA_MODULE_ALL=1 を及ぼして「include "module/all.sra";」相当を全ケースの先頭に効かせる。
#   既に module() を明示するケース(disable_cgal 等)には影響しない(all.sra が読む集合は旧来の
#   eager-load と同じ実カーネル一式。nef は hybrid のみ・d2-d5/demo/pipe_proximity は対象外
#   なのでそれらは引き続き個別に module() が必要)。
export SRAVA_MODULE_ALL=1
# ★ include "module/all.sra" の解決には $SRAVA_PATH (または install 済み SRAVA_LIBDIR) が要る。
#   個別テストの cmake ENVIRONMENT には(元々 include を使わなかったので)大半が設定していない。
#   このスクリプト自身の場所から source tree の lib/ を逆算して補う(cmake 変数に頼らない・
#   このスクリプトがどの build dir からどう呼ばれても効く)。
SRAVA_PATH="$(cd "$(dirname "$0")/../lib" && pwd)"
export SRAVA_PATH
# Windows(MSYS): native srava は "/tmp" を C:\tmp、MSYS sh は C:\msys64\tmp と解決するため
# 出力先とチェック先が食い違う。cygpath で両者一致の native 形へ: D(=cache dir。$D.ext を出力に使う
# ケースを一括で救う)と、直書き /tmp の代替 T。Linux は cygpath 不在 → 従来どおり(/tmp のまま)。
T=/tmp
if command -v cygpath >/dev/null 2>&1; then T=$(cygpath -m /tmp); D=$(cygpath -m "$D"); fi
rm -rf "$D"
case "$MODE" in
callform)
	SRAVA_SOURCE='var mNVF0 = export(union(box(2,2,2), box(1,1,3))); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
cachehit)
	# 同じソースを 2 回実行: 1 回目で生成、2 回目は全部キャッシュ HIT(miss=0)になることを検証。
	HD=/tmp/srava-hit-test; rm -rf "$HD"
	S='export(box(2,2,2) ||| box(1,1,3));'
	SRAVA_CACHE_DIR="$HD" SRAVA_SOURCE="$S" "$SRAVA" >/dev/null 2>&1     # 1 回目(warm)
	SRAVA_CACHE_DIR="$HD" SRAVA_SOURCE="$S" exec "$SRAVA" ;;            # 2 回目(全 HIT)
syscmd)
	# system(cmd): ts2System で非同期実行・完了まで待つ(評価順)。mkdir してから export が成功する。
	SD="$T/srava-sys-test"
	rm -rf "$SD"
	SRAVA_SOURCE="system(\"mkdir -p $SD/sub\"); export(\"$SD/sub/b.stl\", box(2,2,2));" "$SRAVA" >/dev/null 2>&1
	test -f "$SD/sub/b.stl" && echo "SYS_OK" || echo "SYS_FAIL" ;;
sysrc)
	# system の終了コードを式で観測(start_flag を _start 後に立てる修正で可能に)。
	# true→rc==0→ok / false→else。両方正しく分岐すれば SYSRC_OK。
	rm -f "$T/srava-rc-ok.stl" "$T/srava-rc-ng.stl"
	SRAVA_SOURCE="var rc = system(\"true\");  if (rc == 0) { export(\"$T/srava-rc-ok.stl\", box(1,1,1)); }" "$SRAVA" >/dev/null 2>&1
	SRAVA_SOURCE="var rc = system(\"false\"); if (rc == 0) { export(\"$T/srava-rc-ng.stl\", box(1,1,1)); }" "$SRAVA" >/dev/null 2>&1
	if test -f "$T/srava-rc-ok.stl" && ! test -f "$T/srava-rc-ng.stl"; then echo "SYSRC_OK"; else echo "SYSRC_FAIL"; fi ;;
export_regen)
	# 出力ファイルを消して再実行 → 起動時スイープが stale な D_REF を削除し export を再実行 → 再生成。
	RD="$T/srava-regen-cache"; EF="$T/srava-regen-out.stl"
	rm -rf "$RD"; rm -f "$EF"
	S="export(\"$EF\", box(2,2,2));"
	SRAVA_CACHE_DIR="$RD" SRAVA_SOURCE="$S" "$SRAVA" >/dev/null 2>&1     # 生成
	rm -f "$EF"                                                         # 出力を手で削除
	SRAVA_CACHE_DIR="$RD" SRAVA_SOURCE="$S" "$SRAVA" >/dev/null 2>&1     # 再実行(再生成されるはず)
	test -f "$EF" && echo "REGEN_OK" || echo "REGEN_FAIL" ;;
filearg)
	# ソースファイル実行 (srava file.sra) + 先頭シェバング行の読み飛ばし。union = 25v46f。
	F="$D.sra"
	printf '#!/usr/bin/env srava\n// shebang + file 実行テスト\nvar mNVF = export(box(2,2,2) ||| box(1,1,3));\nprint("NVF", nverts(mNVF), nfaces(mNVF));\n' > "$F"
	exec "$SRAVA" "$F" ;;
intersection)
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) &&& box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
difference)
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) --- box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
ifaccum)
	# if + ブロック + 比較 + 自己代入(strict SET)。取られる枝で a を union に更新 → 25v46f
	SRAVA_SOURCE='var a = box(2,2,2); if (1==1) { a = a ||| box(1,1,3); } var mNVF0 = export(a); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
ifskip)
	# 条件偽 → ブロック実行されず a は box のまま → 8v12f
	SRAVA_SOURCE='var a = box(2,2,2); if (1==2) { a = a ||| box(1,1,3); } var mNVF0 = export(a); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
prism)
	SRAVA_SOURCE='var mNVF0 = export(prism(6,2,1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
pyramid)
	SRAVA_SOURCE='var mNVF0 = export(pyramid(4,2,1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
sphere)
	# sphere(r, seg): seg=円周分割数。既定 seg=32 相当 = 八面体 n=8 = 258v/512f(測地球)。
	SRAVA_SOURCE='var mNVF0 = export(sphere(1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
pipeprox_pin_range)
	# ★硬ピンの joint 範囲検査 (2026-08-13)。範囲外の joint は以前 **ヒープを壊していた**:
	#   硬ピンは DOF j+1, j+2 に拘束行を張るので (controller.cpp buildConstraints)、
	#   npts を越えると feasibilityProject の std::vector<Vec3> の外側に書き込み、
	#   in-proc では corrupted double-linked list / process では agent が死んでいた。
	#   今は ①ライブラリ側で弾き ②srava 側が明示エラーを返す。in-proc / process 両方で見る。
	#   併せて **範囲内の硬ピンは従来どおり動く**ことも確認する (弾きすぎの検出)。
	CTRL='[[0,0,0],[5,0,0],[10,0,0]]'
	CTRL5='[[0,0,0],[3,0,0],[6,0,0],[9,0,0],[12,0,0]]'
	bad() {
		rm -rf "$D-pin$1"
		SRAVA_CACHE_DIR="$D-pin$1" \
		SRAVA_SOURCE="module(\"pipe_proximity.so\",{exec_default:\"$1\"}); var r = pipe_adjust($CTRL, 0.8, {dMin:0.5, maxIter:10, pins: [{joint:1, at:[5,1,0], hard:1}]}); print(\"R\", length(r));" \
		  "$SRAVA" 2>&1 | grep -cE 'joint=1 is out of range'
	}
	good() {
		rm -rf "$D-pinok$1"
		SRAVA_CACHE_DIR="$D-pinok$1" \
		SRAVA_SOURCE="module(\"pipe_proximity.so\",{exec_default:\"$1\"}); var r = pipe_adjust($CTRL5, 0.8, {dMin:0.5, maxIter:10, pins: [{joint:1, at:[6,1,0], hard:1}]}); print(\"R\", length(r));" \
		  "$SRAVA" 2>&1 | sed -n 's/^R //p'
	}
	BT=$(bad thread); BP=$(bad process); GT=$(good thread); GP=$(good process)
	echo "out-of-range: in-proc=$BT process=$BP (1=明示エラー)"
	echo "in-range    : in-proc=$GT process=$GP"
	if [ "$BT" != "1" ] || [ "$BP" != "1" ]; then echo "FAIL: 範囲外 pin がエラーにならない (落ちた?)"; exit 0; fi
	if [ -z "$GT" ] || [ "$GT" != "$GP" ]; then echo "FAIL: 範囲内 pin が動かない (in-proc=$GT process=$GP)"; exit 0; fi
	echo "PINRANGE-OK" ;;
# ★ sphere_kernel_agree / tube_kernel_agree は **カーネル一致の表** (#3432) へ移設。
#   test/srava_kernel_agree.sh が モデル×カーネル×許容誤差 を受け取る雛形になっている。
mf_color_3mf)
	# ★#3415 続き: manifold 側の color + 色つき 3MF/AMF export (2026-08-12)。
	# 色の持ち方は cgal (per-face f:color) と違い **頂点プロパティ ch3..5** だが、
	# 出力の 3MF は同じ共通ライタ (common/mesh3mf.h) なので palette/pid の形は同じ。
	#
	# 検証:
	#  ① 3MF に 2 色 (赤+青) の colorgroup が出て、全三角形に pid が付く / 単位が unit 引数どおり
	#  ② **色を付けても幾何が変わらない**: 色つき combine の volume が無色 combine と一致し valid=1
	#  ③ **cache 往復で壊れない** (cold==warm かつ valid=1)
	# ★② は「export した後に同じ式の volume を採る」形で見るのが要点。色が付くと成分の境界で
	#   同一座標の頂点が色ごとに分裂するので、codec が merge ベクタを運ばないと decode 側が
	#   非多様体になり volume=0/valid=0 になる (2026-08-12 の実バグ。この形でだけ再現する)。
	O="$T/srava-mfcolor.3mf"
	rm -f "$O"; rm -rf "$D-c" "$D-w"
	MOD='module("manifold.so",{priority:99}); '
	EXPR='color(box(2,2,2),"red") +++ color(box(1,1,3),"blue")'
	PLAINEXPR='box(2,2,2) +++ box(1,1,3)'
	# export → 同じ式の volume/valid (①②)
	OUT=$(SRAVA_CACHE_DIR="$D-c" \
	      SRAVA_SOURCE="${MOD}export(\"$O\", $EXPR, \"cm\"); print(\"GEO\", volume($EXPR), valid($EXPR)); print(\"PLAIN\", volume($PLAINEXPR));" \
	      "$SRAVA" 2>&1)
	GEO=$(echo "$OUT"   | sed -n 's/^GEO //p')
	PLAIN=$(echo "$OUT" | sed -n 's/^PLAIN //p')
	echo "colored=[$GEO] plain=[$PLAIN]"
	if [ -z "$GEO" ] || [ -z "$PLAIN" ]; then echo "FAIL: no volume printed"; exit 0; fi
	if [ "$GEO" != "$PLAIN 1" ]; then
		echo "FAIL: color changed the geometry (colored=[$GEO] expected=[$PLAIN 1])"; exit 0
	fi
	if [ ! -f "$O" ]; then echo "FAIL: no 3mf written"; exit 0; fi
	# cold / warm の一致 (③)
	CV=$(SRAVA_CACHE_DIR="$D-w" SRAVA_SOURCE="${MOD}print(\"W\", volume($EXPR), valid($EXPR));" "$SRAVA" 2>/dev/null | sed -n 's/^W //p')
	WV=$(SRAVA_CACHE_DIR="$D-w" SRAVA_SOURCE="${MOD}print(\"W\", volume($EXPR), valid($EXPR));" "$SRAVA" 2>/dev/null | sed -n 's/^W //p')
	echo "cold=[$CV] warm=[$WV]"
	if [ "$CV" != "$PLAIN 1" ] || [ "$WV" != "$PLAIN 1" ]; then
		echo "FAIL: colored mesh broke on cache round-trip (cold=[$CV] warm=[$WV])"; exit 0
	fi
	python3 - "$O" <<'PY'
import sys, zipfile, re
d = zipfile.ZipFile(sys.argv[1]).read('3D/3dmodel.model').decode()
pal = re.findall(r'<m:color color="(#[0-9A-Fa-f]{8})"/>', d)
ntri = d.count('<triangle ')
npid = len(re.findall(r'pid="2"', d))
unit = re.search(r'unit="(\w+)"', d).group(1)
print("palette", pal, "tri", ntri, "pid", npid, "unit", unit)
if sorted(pal) != ['#0000FFFF', '#FF0000FF']: print("FAIL: palette", pal)
elif ntri == 0 or npid != ntri:             print("FAIL: pid coverage", npid, "of", ntri)
elif unit != 'centimeter':                  print("FAIL: unit", unit)
else:                                       print("MFCOLOR-OK")
PY
	;;
mf_pipe_scene_inproc)
	# ★#3415 + color/3mf の到達点: **pipe_clearance.sra と同じ形**の連鎖が丸ごと in-proc に乗ること。
	#   map で作ったパス → tube → color(灰) / sphere → color(赤) → combine → 色つき 3MF export。
	#   (プラグイン pipe_proximity への依存だけ外した形。プラグイン自体も in-proc 可なので、
	#    実物の pipe_clearance.sra も同じ条件で完走する)
	#   証明は **存在しない SRAVA_AGENT**: agent プロセスが 1 つでも要るなら 3MF は生まれない。
	O="$T/srava-mfpipe.3mf"
	rm -f "$O"; rm -rf "$D-p"
	PIPE='tube(map([[0,0,0],[6,0,0],[6,5,0],[0,5,0]], \(p){ [p, 0.8]; }), 16)'
	# ★マーカは中心線でなく **管の表面** に置く (pipe_clearance が接近点=表面に置くのと同じ)。
	#   中心線に置くと半径 0.4 の球が半径 0.8 の管に完全に含まれ、mf の combine では吸収されて消える
	#   (mf の combine は包含・重なりを解消する = cg の「交差許容の単純合体」とは意味論が違う)。
	MARK='combine(map([[6.8,2.5,0],[3,-0.8,0]], \(c){ sphere(0.4, 8) >>> c; }))'
	SRAVA_AGENT=/nonexistent/srava_agent SRAVA_CACHE_DIR="$D-p" \
	  SRAVA_SOURCE="module(\"manifold.so\",{priority:99,exec_default:\"thread\"}); export(\"$O\", color($PIPE, \"gray\") +++ color($MARK, \"red\"));" \
	  "$SRAVA" >/dev/null 2>&1
	if [ ! -f "$O" ]; then echo "FAIL: pipe scene needed an agent process (not fully in-proc)"; exit 0; fi
	python3 - "$O" <<'PY'
import sys, zipfile, re
d = zipfile.ZipFile(sys.argv[1]).read('3D/3dmodel.model').decode()
pal = sorted(re.findall(r'<m:color color="(#[0-9A-Fa-f]{8})"/>', d))
ntri = d.count('<triangle ')
print("palette", pal, "tri", ntri)
if pal != ['#969696FF', '#FF0000FF']: print("FAIL: palette", pal)   # gray(150) + red
elif ntri < 100:                      print("FAIL: too few triangles", ntri)
else:                                 print("MFPIPE-INPROC-OK")
PY
	;;
mf_inproc_nested_array)
	# ★in-proc の落とし穴の回帰 (2026-08-12 に mfaTube で発覚): pigDataArray は **要素を eager 解決しない**
	# 設計なので、map/lambda で作った配列の要素は遅延ノードのまま入っている。process 経路は値が
	# テキスト化 → pig_value_parse で素の配列になるので気づかないが、in-proc 経路では遅延ノードが
	# そのまま来て d_cast が null になり「each vertex must be [pos, r]」等の誤エラーになっていた。
	# 対策 = 要素を compact() してから d_cast。ここでは in-proc と process の一致で見る
	# (ネスト配列を取る op = tube(3D パス) と polygon(2D 点列) の 2 本)。
	MAPT='tube(map([[0,0,0],[2,0,0]], \(p){ [p, 0.5]; }), 8)'
	MAPP='polygon(map([0,1,2,3], \(i){ [i*1.0, i*i*1.0]; }))'
	g() {  # $1=exec_default $2=式 $3=計測 op
		SRAVA_CACHE_DIR="$D-$1-$3" SRAVA_SOURCE="module(\"manifold.so\",{priority:99,exec_default:\"$1\"}); print(\"R\", $3($2));" \
		  "$SRAVA" 2>/dev/null | sed -n 's/^R //p'
	}
	rm -rf "$D-thread-volume" "$D-process-volume" "$D-thread-area" "$D-process-area"
	TT=$(g thread  "$MAPT" volume); TP=$(g process "$MAPT" volume)
	PT=$(g thread  "$MAPP" area);   PP=$(g process "$MAPP" area)
	echo "tube    in-proc=$TT process=$TP"
	echo "polygon in-proc=$PT process=$PP"
	# pipe_proximity: bodies = map で作った **ハッシュの配列** (ネストが 1 段深い)。
	# hash の値も配列の要素と同じく遅延ノードで来るので、同じゲートウェイが要る。
	MKB='var mk = \(y){ var h = {ctrl: [[0,y,0],[5,y,0],[10,y,0]], radius: 0.8, movable: 1}; h; }; var b = map([0.0,2.0], mk);'
	gb() {
		rm -rf "$D-pp$1"
		SRAVA_CACHE_DIR="$D-pp$1" \
		SRAVA_SOURCE="module(\"pipe_proximity.so\",{exec_default:\"$1\"}); $MKB print(\"R\", length(pipe_scene_proximity(b, 8.0)));" \
		  "$SRAVA" 2>/dev/null | sed -n 's/^R //p'
	}
	BT=$(gb thread); BP=$(gb process)
	echo "bodies  in-proc=$BT process=$BP"
	if [ -n "$TT" ] && [ "$TT" = "$TP" ] && [ -n "$PT" ] && [ "$PT" = "$PP" ] && [ -n "$BT" ] && [ "$BT" = "$BP" ]
	then echo "NESTED-INPROC-OK"; else echo "FAIL: in-proc/process mismatch"; fi ;;
mf_tube_inproc)
	# ★#3415 の眼目: tube が manifold にも在ることで、tube 主体の連鎖が丸ごと in-proc に乗る。
	# 証明は「**存在しない SRAVA_AGENT** を渡して完走するか」(2026-08-06 の検証手法)。
	# agent プロセスが 1 つでも要る = cgal(process)に落ちた、なら export は生まれない。
	# 対照として cgal を最優先にした同じ式が **失敗する**ことも見る(テストが空振りでない証拠)。
	O="$T/srava-mftube-inproc.stl"; O2="$T/srava-mftube-ctl.stl"
	rm -f "$O" "$O2"; rm -rf "$D-mf" "$D-cg"
	EXPR='tube([[[0,0,0],0.5],[[2,0,0],0.5],[[2,3,1],0.4]], 16) ||| box(1,1,1)'
	SRAVA_AGENT=/nonexistent/srava_agent SRAVA_CACHE_DIR="$D-mf" \
	  SRAVA_SOURCE="module(\"manifold.so\",{priority:99,exec_default:\"thread\"}); export(\"$O\", $EXPR);" \
	  "$SRAVA" >/dev/null 2>&1
	SRAVA_AGENT=/nonexistent/srava_agent SRAVA_CACHE_DIR="$D-cg" \
	  SRAVA_SOURCE="module(\"manifold.so\",{priority:99,exec_default:\"thread\"}); module(\"cgal.so\",{priority:100}); export(\"$O2\", $EXPR);" \
	  "$SRAVA" >/dev/null 2>&1
	if [ ! -f "$O" ]; then echo "FAIL: manifold tube chain needed an agent process"; exit 0; fi
	if [ -f "$O2" ]; then echo "FAIL: cgal control unexpectedly ran without an agent"; exit 0; fi
	echo "MFTUBE-INPROC-OK" ;;
arrayidx)
	# array リテラル + 添字参照: a[0] ||| a[1] = union(box,box) = 25v46f
	SRAVA_SOURCE='var a = [box(2,2,2), box(1,1,3)]; var mNVF0 = export(a[0] ||| a[1]); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
hashlit)
	# source 側 hash リテラル {k:v,..} + メンバ参照: h.a ||| h.b = union(box,box) = 25v46f
	SRAVA_SOURCE='var h = {a: box(2,2,2), b: box(1,1,3)}; var mNVF0 = export(h.a ||| h.b); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
inlineval)
	# 構造 inline 引数: array を serialize→wire→agent で value-parse→cgaBox 展開。
	# hash メンバ→array も経由。boxa([1,1,3]) = 直方体 = 8v12f。
	SRAVA_SOURCE='var h = {dims: [1,1,3]}; var mNVF0 = export(boxa(h.dims)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
lambda)
	# lambda + apply(clone/thunk): u(box(2,2,2)) = box(2,2,2) ||| box(1,1,3) = 25v46f。
	SRAVA_SOURCE='var u = \(s){ s ||| box(1,1,3); }; var mNVF0 = export(u(box(2,2,2))); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
lambda_reapply)
	# 同一 lambda を別引数で再 apply(body->clone() でメモ衝突回避)。2 union の union = 33v62f。
	SRAVA_SOURCE='var u = \(s){ s ||| box(1,1,3); }; var mNVF0 = export(u(box(2,2,2)) ||| u(box(3,3,3))); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
lambda_closure)
	# クロージャ(カリー化): adder(a) が a を捕捉した lambda を返す。add1(box(1,1,3)) = 25v46f。
	SRAVA_SOURCE='var adder = \(a){ \(b){ a ||| b; }; }; var add1 = adder(box(2,2,2)); var mNVF0 = export(add1(box(1,1,3))); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
closure_capture)
	# 値捕捉(by-value): f は定義時の base=4 を凍結 → 後の base=5(set_var)に影響されない。
	# late-binding なら f()==5 で分岐せず CAPTURE_OK が出ない。env-snapshot の回帰テスト。
	SRAVA_SOURCE='var base=4; var f=\(){base;}; base=5; if (f()==4) { print("CAPTURE_OK"); }' exec "$SRAVA" ;;
closure_deepcopy)
	# 配列/ハッシュの値捕捉は spine を deep copy: 捕捉後の破壊代入 base[0]=9 が漏れない(f()==1)。
	# shallow(参照共有)なら f()==9 で分岐せず DEEPCOPY_OK が出ない。capture_copy の回帰テスト。
	SRAVA_SOURCE='var base=[1,2,3]; var f=\(){ base[0]; }; base[0]=9; if (f()==1) { print("DEEPCOPY_OK"); }' exec "$SRAVA" ;;
workergate)
	# ワーカーゲート: cap が木の深さより小さくてもデッドロックせず完走する(タイマー緩和の回帰)。
	# 同時 agent 上限 2 (SRAVA_LOAD_CPU=0 + SRAVA_LOAD_AGENT=2) は ENVIRONMENT で注入。union(6 箱)の二分木は深さ>2。完走すれば末尾サマリが出る。
	SRAVA_SOURCE='export(union([box(1,1,1), box(1,1,1)>>>[2,0,0], box(1,1,1)>>>[4,0,0], box(1,1,1)>>>[0,2,0], box(1,1,1)>>>[2,2,0], box(1,1,1)>>>[4,2,0]]));' exec "$SRAVA" ;;
workergate_eagain)
	# PIG_TEST_FORKLIMIT=2(同時 fork>2 を失敗させる)< ゲート上限 32(高め・env)。
	# limit 固定方針なので backoff せず fork/process limit 超過の明確なエラーで終了する
	# (黙ったデッドロック/ハングを避け、ユーザに cap を下げて再実行してもらう)。
	SRAVA_SOURCE='export(union([box(1,1,1), box(1,1,1)>>>[2,0,0], box(1,1,1)>>>[4,0,0], box(1,1,1)>>>[0,2,0], box(1,1,1)>>>[2,2,0], box(1,1,1)>>>[4,2,0]]));' exec "$SRAVA" ;;
module_throw_inproc|module_throw_process)
	# ★ モジュールが投げた例外を **ホスト側 (ptsCalcBody) の安全網**が受け止めること (ひさ判断 2026-08-26)。
	#   d4 は exec_caps=THREAD|PROCESS なので、同じフック (PIG_TEST_MODULE_THROW) で両方試せる。
	#
	#   in-proc  : 例外は **planner と同じプロセス**の専用スレッドで飛ぶ。網が無ければ
	#              planner ごと terminate = **rc=134 (SIGABRT)** で計測も何も残らない (実測で確認済み)。
	#   process  : 例外は agent プロセスで飛ぶ。網が無ければ agent が SIGABRT で死に、planner は
	#              "agent died with SIGABRT" と報告する (原因は stderr 経由でしか分からない)。
	#   ★ どちらも網があれば **op のエラー**として返り、srava は rc=1 で正常に終了する。
	#   ⚠ **rc=134 でないこと**を明示的に見る = 「エラーとして返った」と「プロセスが死んだ」の区別。
	case "$MODE" in
	*inproc)  EXEC=thread  ;;
	*)        EXEC=process ;;
	esac
	OUT=$(PIG_TEST_MODULE_THROW=1 SRAVA_SOURCE="module(\"d4.so\", {priority:99, exec_default:\"$EXEC\"});
	      print(d4_nfaces(d4_cube(1)));" "$SRAVA" 2>&1); RC=$?
	[ "$RC" != "134" ] || { echo "FAIL: exec=$EXEC でプロセスがシグナル死した (rc=134・網が効いていない)"; echo "$OUT"; exit 1; }
	[ "$RC" != "0" ]   || { echo "FAIL: exec=$EXEC で例外を投げたのに rc=0"; echo "$OUT"; exit 1; }
	echo "$OUT" | grep -q 'ERROR' || { echo "FAIL: exec=$EXEC でエラーが報告されない"; echo "$OUT"; exit 1; }
	echo "$OUT" | grep -q 'uncaught exception' || {
		echo "FAIL: exec=$EXEC のエラー文に 'uncaught exception' が無い"; echo "$OUT"; exit 1; }
	# フックを外せば普通に通ること (網が正常系を壊していない)
	OUT2=$(SRAVA_SOURCE="module(\"d4.so\", {priority:99, exec_default:\"$EXEC\"});
	       print(d4_nfaces(d4_cube(1)));" "$SRAVA" 2>&1)
	echo "$OUT2" | grep -q 'result value' || { echo "FAIL: exec=$EXEC の正常系が壊れた"; echo "$OUT2"; exit 1; }
	echo "MODULE-THROW-OK exec=$EXEC rc=$RC" ;;
gate_side_error)
	# ★ gate(x, side) の **side がエラーのとき無音にならない** こと (ひさ指摘 2026-08-26)。
	#   gate は第 2 引数を ptsFireAndForget で起動して **値を捨てる**ので、誰も結果を見ない。
	#   直す前は `gate(box(1,1,1), volume(1))` が **rc=0 / result value=1.0** で成功していた
	#   (side effect の失敗が完全に無音)。→ ptsApplication へ報告して終了させる配線を入れた。
	#   ⚠ async は呼び手 (planner) が drain して報告するので、そちらは二重報告しないこと。
	OUT=$(SRAVA_SOURCE='print(volume(gate(box(1,1,1), volume(1))));' "$SRAVA" 2>&1); RC=$?
	[ "$RC" != "0" ] || { echo "FAIL: gate の側効果がエラーなのに rc=0"; echo "$OUT"; exit 1; }
	echo "$OUT" | grep -q 'ERROR' || { echo "FAIL: gate の側効果のエラーが報告されない"; echo "$OUT"; exit 1; }
	# 正常系は不変 (側効果が走り、値は第 1 引数のまま)
	OUT2=$(SRAVA_SOURCE='print(volume(gate(box(2,2,2), print("SIDE"))));' "$SRAVA" 2>&1)
	echo "$OUT2" | grep -q 'SIDE' || { echo "FAIL: gate の側効果が走っていない"; echo "$OUT2"; exit 1; }
	echo "$OUT2" | grep -q 'result value=8' || { echo "FAIL: gate の値が第 1 引数でない"; echo "$OUT2"; exit 1; }
	# async は二重報告しない (drain が 1 度だけ出す)
	N=$(SRAVA_SOURCE='async { print(volume(1)); }
print("after");' "$SRAVA" 2>&1 | grep -c '\*\*\* ERROR')
	[ "$N" = "1" ] || { echo "FAIL: async のエラー報告が $N 回 (期待 1)"; exit 1; }
	echo "GATE-SIDE-ERROR-OK" ;;
fdleak)
	# fd リーク回帰: ulimit -n を 64 に絞って ~240 agent を回す。FIN で rfd/pipe を閉じないと
	# fd が枯渇し pipe()/fork が EMFILE で "failed to launch agent"(macOS 256 で顕在化した真因)。
	# 修正後は fd が再利用され完走して "result cache" が出る。
	#
	# NB(2026-08-01 実測): 定常時のピーク fd 使用量は同時 agent 数にほぼ比例し、最小で通る
	#   ulimit -n は W8:60 / W6:48 / W4:40 / W2:32。現行(W8)は上限 64 に対し余裕 4 fd と薄い。
	#   ただし本テストが cold 実行で稀に落ちる主因は fd ではなく **agent との相互待ちハング**
	#   (fd 枯渇は必ず 0.1s で "fork failed" エラー終了する。ハングは 60s 無応答)。
	#   ハングは同時実行数依存で、12 並列なら ~1/12 で再現する(perf/hang_repro.sh)。
	ulimit -n 64 2>/dev/null
	SRAVA_SOURCE='var p=[]; var i; for(i=0;i<80;i=i+1){ p=concat(p, prism(3+i,2,1)>>>[i*1.0,0,0]); } export("/tmp/srava-fdleak.stl", combine(p));' exec "$SRAVA" ;;
while_loop)
	# while(毎周 clone 再評価): i=1,2 で box(i,i,9) を union 蓄積。box(5,5,5)|||box(1,1,9)|||box(2,2,9)
	# = 52v100f。i が進む(共有 env への代入)+ 毎周別形状(clone)を検証。
	SRAVA_SOURCE='var i = 1; var acc = box(5,5,5); while (i < 3) { acc = acc ||| box(i,i,9); i = i + 1; } var mNVF0 = export(acc); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
for_loop)
	# for(init;cond;step) → while desugar。while_loop と等価 = 52v100f。
	SRAVA_SOURCE='var acc = box(5,5,5); for (var i = 1; i < 3; i = i + 1) { acc = acc ||| box(i,i,9); } var mNVF0 = export(acc); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
for_nested)
	# 入れ子 for(i,j 各 1..2)。内側 for は外側 body の clone で毎周新鮮 j に再初期化される。
	# box(9,9,9)|||box(i,j,7) 4 個 = 33v62f。
	SRAVA_SOURCE='var acc = box(9,9,9); for (var i = 1; i < 3; i = i+1) { for (var j = 1; j < 3; j = j+1) { acc = acc ||| box(i,j,7); } } var mNVF0 = export(acc); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
apply_chain)
	# 一般呼び出し: 中間変数なしの直接/連鎖適用。adder(box)(box) = カリー化を直に適用 = 25v46f。
	SRAVA_SOURCE='var adder = \(a){ \(b){ a ||| b; }; }; var mNVF0 = export(adder(box(2,2,2))(box(1,1,3))); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
recursion)
	# 再帰 lambda(引数 call-by-value で変数捕捉を回避)。box(i,i,9) を i=2,1 と再帰 union、
	# 基底 box(5,5,5)。= 52v100f(while/for 版と同形状)。**自己適用**形の再帰を検証。
	# ★ 2026-08-29 (ひさ設計・#3450): クロージャの凍結 env は親を持たなくなった
	#   (frozen->parent = thNULL)。名前による自己再帰 `var f = \(n){ … f(n-1) … }` は
	#   f が前方参照になるため「undefined variable: f」の明示エラー。再帰は自分を引数で
	#   渡す自己適用 `var f = \(f,n){ … f(f,n-1) … }; f(f,2)` で書く。
	SRAVA_SOURCE='var f = \(f,n){ if (n < 1) { box(5,5,5); } else { box(n,n,9) ||| f(f, n - 1); } }; var mNVF0 = export(f(f,2)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
import)
	# import(path): box(2,2,2) を STL に書き → import で読み戻し(soup repair)→ box(1,1,3) と union
	# = 25v46f。export→import 往復 + 拡張子判別 + DAG 葉としての利用を検証。
	SRAVA_SOURCE='export("/tmp/srava-import-test.stl", box(2,2,2)); var mNVF0 = export(import("/tmp/srava-import-test.stl") ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
nary)
	# 実行木分解: n-ary 可換呼び出し union(a,b,c) をプランナーが二項木に分解(agent は二項のみ)。
	# 3 box の union = 27v50f。
	SRAVA_SOURCE='var mNVF0 = export(union(box(2,2,2), box(1,1,3), box(5,5,5))); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
import_err)
	# import 失敗(不在ファイル)は サイレント空メッシュでなく明示エラーになること(nv= を出さない)。
	rm -f /tmp/srava-import-missing.stl
	SRAVA_SOURCE='export(import("/tmp/srava-import-missing.stl"));' exec "$SRAVA" ;;
import_err_union)
	# 失敗 import が下流 module(union)の上流にある場合: クリーンな import エラーが伝播し(arg type
	# /index mismatch でなく)、起動済み orphan agent でハングしないこと(TIMEOUT で検出)。
	rm -f /tmp/srava-import-missing2.stl
	SRAVA_SOURCE='export(import("/tmp/srava-import-missing2.stl") ||| box(1,1,3));' exec "$SRAVA" ;;
xlate)
	# translate(m,x,y,z): box を +1 移動して原位置 box と union(重なり)= 24v44f。座標が動く証明。
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) ||| translate(box(2,2,2), 1, 0, 0)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
rotate)
	# rotate(m,axis,deg): 45° 回転(任意角→double cos/sin)。原位置 box と union で星型重なり = 22v40f。
	SRAVA_SOURCE='var mNVF0 = export(box(4,4,1) ||| rotate(box(4,4,1), "z", 45)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
mirror)
	# mirror(m,axis): x=3 に寄せた box を x 鏡像 → x=-3。union で分離 2 個 = 16v24f。向き補正で union 成立。
	SRAVA_SOURCE='var mNVF0 = export(translate(box(1,2,2), 3,0,0) ||| mirror(translate(box(1,2,2), 3,0,0), "x")); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
xform)
	# transform(m,matrix): 3x4 行列の平行移動列で +1 移動 = translate と等価。union 重なり = 24v44f。
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) ||| transform(box(2,2,2), [1,0,0,1, 0,1,0,0, 0,0,1,0])); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
rotate_err)
	# 未対応 axis は明示エラー(サイレント無視でない)。
	SRAVA_SOURCE='export(rotate(box(1,1,1), "w", 30));' exec "$SRAVA" ;;
op_xlate)
	# 演算子 >>> = translate。||| より強く結合(括弧なし)→ box ||| (box>>>[1,0,0]) = 24v44f。
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) ||| box(2,2,2) >>> [1,0,0]); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
op_rotate)
	# 演算子 @(axis,d) = rotate。box(4,4,1) ||| (box @ ("z",45)) = 22v40f。
	SRAVA_SOURCE='var mNVF0 = export(box(4,4,1) ||| box(4,4,1) @ ("z", 45)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
op_mirror)
	# 演算子 <> = mirror。x=3 の箱を <>"x" で x=-3 へ、union 分離 = 16v24f。
	SRAVA_SOURCE='var mNVF0 = export((box(1,2,2) >>> [3,0,0]) ||| (box(1,2,2) >>> [3,0,0]) <> "x"); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
vec_axis)
	# ベクトル軸回転 rotate(m,[0,0,1],deg) は文字列 "z" と同結果(任意軸 Rodrigues の主軸特例)= 22v40f。
	SRAVA_SOURCE='var mNVF0 = export(box(4,4,1) ||| rotate(box(4,4,1), [0,0,1], 45)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
vec_degenerate)
	# 退化軸ベクトル [0,0,0] は明示エラー(正規化不能)。
	SRAVA_SOURCE='export(rotate(box(1,1,1), [0,0,0], 30));' exec "$SRAVA" ;;
scale_uniform)
	# 均等スケール: box(1,1,1)*2 = box(2,2,2) と完全重複 → union 8v12f。
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) ||| scale(box(1,1,1), 2)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
scale_op)
	# 演算子 *** 均等。box(1,1,1)***2 = box(2,2,2) と重複 8v12f。
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) ||| box(1,1,1) *** 2); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
scale_vec)
	# 軸別スケール(配列)= 3スカラと同一(計算本体で判別)。離れた位置で union 確認。
	SRAVA_SOURCE='var mNVF0 = export(box(1,1,1) ||| (scale(box(1,1,1), [2,3,4]) >>> [5,0,0])); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
scale_err)
	# 退化(0)スケールは明示エラー(メッシュが潰れる)。
	SRAVA_SOURCE='export(scale(box(1,1,1), 0));' exec "$SRAVA" ;;
neg_literal)
	# 単項マイナス: 負方向移動 box>>>[-3,0,0] は元 box と分離 → 16v24f。配列内負値の round-trip 検証。
	SRAVA_SOURCE='var mNVF0 = export(box(1,1,1) >>> [-3,0,0] ||| box(1,1,1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
extrude)
	# 2D→3D: rect(2,1) を高さ 3 で extrude = 直方体 8v12f。cgMesh2D(PLY2)→ reader 多態 → cgMesh3D。
	SRAVA_SOURCE='var mNVF0 = export(extrude(rect(2,1), 3)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
poly2d_union)
	# 2D ブーリアン(Polygon_set_2)+ 2D translate(apply_affine)。重なる 2 正方形の和 = L 字、extrude。
	SRAVA_SOURCE='var mNVF0 = export(extrude(rect(2,2) ||| (rect(2,2) >>> [1,1,0]), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
extrude_union3d)
	# 2D→3D extrude した角柱が 3D ブール(corefinement)に乗る(多態スピンの end-to-end)。
	SRAVA_SOURCE='var mNVF0 = export(box(5,5,5) ||| extrude(rect(2,1), 3)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
mixed_dim_err)
	# 2D ||| 3D は型ガードで明示エラー(op_union が null → A_ERROR)。
	SRAVA_SOURCE='export(rect(2,2) ||| box(1,1,1));' exec "$SRAVA" ;;
prim_ngon)
	# 正六角形 extrude = 六角柱 12v20f。任意角頂点(cos/sin)。
	SRAVA_SOURCE='var mNVF0 = export(extrude(ngon(6, 1), 2)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
prim_circle)
	# 円(32 角形近似)extrude = 円柱 64v124f。
	SRAVA_SOURCE='var mNVF0 = export(extrude(circle(1), 2)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
prim_circle_segs)
	# circle 精度ピッチ(第2引数=辺数): circle(1,8)=八角形 → extrude 八角柱 16v28f。
	SRAVA_SOURCE='var mNVF0 = export(extrude(circle(1, 8), 2)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
prim_sphere_subdiv)
	# icosphere(r, subdiv): subdiv=細分回数(二十面体を 2^subdiv 分割)。icosphere(1,2)=162v/320f。
	# 旧 sphere(1,2) の subdiv 意味論はこの op が継ぐ(sphere は seg 意味論に変更)。
	SRAVA_SOURCE='var mNVF0 = export(icosphere(1, 2)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
prim_polygon)
	# 明示点列(時計回りでも CCW 正規化)→ 三角柱 6v8f。
	SRAVA_SOURCE='var mNVF0 = export(extrude(polygon([[0,0],[1,2],[2,0]]), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
prim_polygon_err)
	# 2 点は多角形でない → 明示エラー。
	SRAVA_SOURCE='export(extrude(polygon([[0,0],[1,1]]), 1));' exec "$SRAVA" ;;
extrude_hole)
	# 穴対応 extrude(CDT): 4x4 から中央 2x2 を引いた額縁を立体化 → トンネル付きプリズム 16v32f。
	SRAVA_SOURCE='var mNVF0 = export(extrude(rect(4,4) --- (rect(2,2) >>> [1,1,0]), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
extrude_hole_union)
	# 額縁プリズムが閉多様体・向き正しい証明: box との corefinement union が通る → 19v34f。
	SRAVA_SOURCE='var mNVF0 = export(box(10,10,10) ||| extrude(rect(4,4) --- (rect(2,2) >>> [1,1,0]), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
rotate2d)
	# 2D rotate 軸不要(単一角度=z 面内回転)。演算子 @(deg)。位相は 8v12f。
	SRAVA_SOURCE='var mNVF0 = export(extrude(rect(2,1) @ (45), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
area_expr)
	# 値返し op(area)を式で観測(cold cache/MISS 経路)。2D 面積 rect(2,3)=6 を == で判定 →
	# 真なら union(25v46f)。値の VALUE 復元 + 演算子の継続 deref + A_SAVE_BEGIN 本文を検証。
	SRAVA_SOURCE='if (area(rect(2,3)) == 6) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
area_arith)
	# 値の算術 + 2 つの値 op 比較(volume(v1)==volume(v2) パターン)。area(2,3)+area(1,1)=7 → 25v46f。
	SRAVA_SOURCE='var s = area(rect(2,3)) + area(rect(1,1)); if (s == 7) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
cachedir_ctl)
	# task2: キャッシュ dir 初期化を first-agent 頭へ移動 + mkdir -p。
	# プログラムが CACHE_DIR を(env 既定を上書きして)深い新規 dir に設定 → mkdir -p で作成し
	# そこに cache が落ちる。export 成功(25v46f)= mkdir -p と first-agent 初期化が効いている証拠。
	D="/tmp/srava-cachectl-deep/x/y/z"
	rm -rf /tmp/srava-cachectl-deep
	SRAVA_SOURCE="CACHE_DIR = \"$D\"; var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print(\"NVF\", nverts(mNVF0), nfaces(mNVF0));" exec "$SRAVA" ;;
lexical_shadow)
	# レキシカルスコープ(eager-DEF): var b は外側 sz.w=2 で定義 → 内側ブロックで sz=0 が
	# シャドウしても b は外側 sz を参照(dynamic scope なら 0.w でエラー)。box(2,2,2)|||box(1,1,3)=25v46f。
	SRAVA_SOURCE='var sz={w:2}; var b=box(sz.w,sz.w,sz.w); { var sz=0; var mNVF0 = export(b ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); }' exec "$SRAVA" ;;
arr_varref)
	# 配列構築 `[..]` を演算子化(pigDataOperatorArray)した回帰: インライン配列内の varref が
	# **ネストした agent op(union の mesh 引数)**でも正しい env で解決される。
	# box(1,1,1) を [d,0,0] で平行移動 → box(2,2,2) と非接触 union = 2 箱 = 16v24f。
	SRAVA_SOURCE='var d=4; var mNVF0 = export(box(2,2,2) ||| (box(1,1,1) >>> [d,0,0])); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
export_unit)
	# SVG/DXF の単位指定(export の 3 番目の引数)。SVG=width/height、DXF=$INSUNITS に反映。
	O="$T/srava-exunit-out"; rm -rf "$O"; mkdir -p "$O"
	SRAVA_SOURCE="export(\"$O/u.svg\", rect(260,135), \"mm\"); export(\"$O/u.dxf\", rect(260,135), \"mm\");" "$SRAVA" >/dev/null 2>&1
	if grep -q 'width="260mm"' "$O/u.svg" && grep -q 'INSUNITS' "$O/u.dxf" ; then
		echo "EXPORT_UNIT_OK"
	else
		echo "EXPORT_UNIT_FAIL (svg/dxf unit missing)"
	fi ;;
parallel_cmp)
	# trigger(並列 spark): 独立した 2 つの値 op を比較 → 両 agent を並列起動。
	# 正当性検証(8 != 3 → false → else の union 25v46f)。並列性自体は手動計測(PIG_TEST_SLOW)で確認。
	SRAVA_SOURCE='if (volume(box(2,2,2)) == volume(box(1,1,3))) { var mNVF0 = export(box(1,1,1)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
valid_ok)
	# 検査(値返し): 健全な 3D box は valid==1 → 真なら union(25v46f)。値 VALUE 復元を検証。
	SRAVA_SOURCE='if (valid(box(2,2,2)) == 1) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
valid_bad)
	# 自己交差 2D(bowtie)を polygon() で作れる(検査緩和)→ valid==0 を検出 → 真なら 25v46f。
	SRAVA_SOURCE='if (valid(polygon([[0,0],[2,2],[2,0],[0,2]])) == 0) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
repair_2d)
	# 2D 修復: bowtie を repair(even-odd)→ 2 三角形に正規化 → valid==1。repair(mesh 返し)+valid(値)合成。
	SRAVA_SOURCE='if (valid(repair(polygon([[0,0],[2,2],[2,0],[0,2]]))) == 1) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
repair_3d_selfx)
	# ★ #3442 追補: 3D repair (autorefine) は **自己交差を解消しない** ことを固定する。
	#   以前ソースに「とぐろ tube 等の自己交差を valid(=1) に持ち込む」と書いてあったが誤りで、
	#   実測ではどの自己交差でも repair 後 valid=0 のままだった。テストが健全な箱しか見ていなかった
	#   ので誰も気づいていなかった → ここで実態を固定する。
	#   ★もしここが REPAIRSELFX_OK でなく「1 1」になったら、repair が本当に直せるようになった合図
	#   (良い変化なので、そのときはテストと doc を更新する)。
	S='tube([[[0,0,0],0.5],[[4,0,0],0.5],[[4,2,0],0.5],[[2,2,0],0.5],[[2,-1,0],0.5]], 10)'
	A=$(SRAVA_SOURCE="print(\"A\", valid($S));" "$SRAVA" 2>&1 | sed -n 's/^A //p')
	B=$(SRAVA_SOURCE="print(\"B\", valid(repair($S)));" "$SRAVA" 2>&1 | sed -n 's/^B //p')
	# 細分は効いていること (交差線が実エッジになる)
	N0=$(SRAVA_SOURCE="print(\"N\", nfaces($S));" "$SRAVA" 2>&1 | sed -n 's/^N //p')
	N1=$(SRAVA_SOURCE="print(\"N\", nfaces(repair($S)));" "$SRAVA" 2>&1 | sed -n 's/^N //p')
	if [ "$A" != "0" ] ; then echo "REPAIRSELFX_FAIL: 前提の形状が自己交差していない (valid=$A)" ; exit 0 ; fi
	if [ "$B" != "0" ] ; then
		echo "REPAIRSELFX_FAIL: repair 後 valid=$B になった = 直せるようになった (doc/テストを更新すべき)"
		exit 0
	fi
	ok=$(awk -v a="$N0" -v b="$N1" 'BEGIN{ print (a!="" && b!="" && b+0 > a+0) ? 1 : 0 }')
	if [ "$ok" != "1" ] ; then
		echo "REPAIRSELFX_FAIL: 細分が効いていない (面数 $N0 → $N1)" ; exit 0
	fi
	echo "REPAIRSELFX_OK 自己交差は残る (valid $A→$B) が細分は効く (面数 $N0→$N1)" ;;

repair_3d)
	# 3D 修復: 健全 box は autorefine 無変化 → cache を経て union が通る(repair が usable mesh を返す証明)。25v46f。
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) ||| repair(box(1,1,3))); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
volume)
	# 計測(値返し): 3D box(2,2,2) の体積=8 → 真なら 25v46f。発散定理ベース。
	SRAVA_SOURCE='if (volume(box(2,2,2)) == 8) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
volume_err)
	# 2D に体積はない → エラー(area を使えと案内)。
	SRAVA_SOURCE='export(volume(rect(2,3)));' exec "$SRAVA" ;;
perimeter)
	# 計測(値返し): 2D rect(2,3) の境界長=2*(2+3)=10 → 真なら 25v46f。
	SRAVA_SOURCE='if (perimeter(rect(2,3)) == 10) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
centroid_2d)
	# 計測(配列返し): rect(2,3) の面積重心=[1,1.5]。配列 VALUE 復元 + 添字 c[0]/c[1] を検証 → 25v46f。
	SRAVA_SOURCE='var c = centroid(rect(2,3)); if (c[0] == 1) { if (c[1] == 1.5) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); } } else { var mNVF2 = export(box(1,1,1)); print("NVF", nverts(mNVF2), nfaces(mNVF2)); }' exec "$SRAVA" ;;
centroid_3d)
	# 計測(配列返し): box(2,2,2) の体積重心=[1,1,1]。3 要素配列の添字 c[2] を検証 → 25v46f。
	SRAVA_SOURCE='var c = centroid(box(2,2,2)); if (c[2] == 1) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
distance)
	# 近接(値返し・二項): box [0,1]^3 と +3 平行移動した box の最近接距離=2(x=1 と x=3 の隙間)→ 25v46f。
	SRAVA_SOURCE='if (distance(box(1,1,1), box(1,1,1) >>> [3,0,0]) == 2) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
distance_err)
	# 近接は 3D 専用。2D 入力はエラー。
	SRAVA_SOURCE='export(distance(rect(1,1), rect(2,2)));' exec "$SRAVA" ;;
closest)
	# 近接(配列返し): [dist,[pa],[pb]]。dist=2 かつ pa.x=1(近接面)を**入れ子添字 c[1][0]** で検証 → 25v46f。
	SRAVA_SOURCE='var c = closest(box(1,1,1), box(1,1,1) >>> [3,0,0]); if (c[0] == 2) { if (c[1][0] == 1) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); } } else { var mNVF2 = export(box(1,1,1)); print("NVF", nverts(mNVF2), nfaces(mNVF2)); }' exec "$SRAVA" ;;
farthest)
	# 近接(配列返し・頂点総当り厳密): 対角隅 (0,0,0)-(4,1,1) → √18≈4.24 > 4 → 25v46f。
	SRAVA_SOURCE='var c = farthest(box(1,1,1), box(1,1,1) >>> [3,0,0]); if (c[0] > 4) { var mNVF0 = export(box(2,2,2) ||| box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0)); } else { var mNVF1 = export(box(1,1,1)); print("NVF", nverts(mNVF1), nfaces(mNVF1)); }' exec "$SRAVA" ;;
tube)
	# 3D 掃引管: 直線パス 2 頂点・半径 0.5・八角断面。側面 8 帯 + 両端平キャップ = 18v32f。
	SRAVA_SOURCE='var mNVF0 = export(tube([[[0,0,0],0.5],[[0,0,3],0.5]], 8)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
bigtube)
	# #4 性能崖の回帰ガード: 2048 点の巨大インライン配列(serialize 後 ~157KB > 64KB 既定パイプ)。
	# 修正前は planner→agent の pipe 送信が EAGAIN yield の resume 不全で停止(>40s〜ハング)。
	# 修正(agent stdin の F_SETPIPE_SZ 拡張)後は ~2s。TIMEOUT で崖の再発を検知する。
	PTS=$(python3 -c "import math;print(','.join('[[%g,%g,%g],0.3]'%(round(math.cos(i*0.05),4),round(math.sin(i*0.05),4),round(i*0.02,4)) for i in range(2048)))")
	SRAVA_SOURCE="var mNVF0 = export(tube([$PTS], 6)); print(\"NVF\", nverts(mNVF0), nfaces(mNVF0));" exec "$SRAVA" ;;
tube_taper)
	# 太さ可変 + 端 r=0(尖り): 始端 apex(円錐)/終端 平キャップ。八角。10v16f。
	SRAVA_SOURCE='var mNVF0 = export(tube([[[0,0,0],0],[[0,0,3],0.5]], 8)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
tube_union)
	# 管が閉多様体・外向き正しい証明: box との corefinement union が通る → 34v64f。
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) ||| tube([[[0,0,0],0.5],[[0,0,4],0.5]], 8)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
tube_dedup)
	# 連続重複頂点を弾かず間引く: 重複を含むパスでも、間引き後 2 頂点の素の管(18v32f)になる。
	SRAVA_SOURCE='var mNVF0 = export(tube([[[0,0,0],0.5],[[0,0,0],0.5],[[0,0,3],0.5]], 8)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
tube2d)
	# 2D 次元ディスパッチ: 位置が [x,y] なら可変半幅の帯(cgMesh2D)。valid な単一領域になることを確認。
	SRAVA_SOURCE='var v = valid(tube([[[0,0],3],[[20,5],2],[[35,-8],4]])); if (v == 1) { print("TUBE2D_OK"); }' exec "$SRAVA" ;;
revolve)
	# 2D→3D 回転体: rect[0,1]x[0,2] を Y 軸 360° → 円柱(半径1高2)。軸接辺は潰れる。66v128f。
	SRAVA_SOURCE='var mNVF0 = export(revolve(rect(1,2), 360)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
revolve_union)
	# 円柱が閉多様体・向き正しい証明: box との corefinement union が通る → 70v136f。
	SRAVA_SOURCE='var mNVF0 = export(box(5,5,5) ||| revolve(rect(1,2), 360)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
revolve_partial)
	# 部分角(90°扇形柱)= 両端に CDT キャップ付き閉立体。20v36f。
	SRAVA_SOURCE='var mNVF0 = export(revolve(rect(1,2), 90)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
revolve_segs)
	# 回転分割数(第3引数=回転ピッチ): 8 分割の粗い円柱 → 18v32f。
	SRAVA_SOURCE='var mNVF0 = export(revolve(rect(1,2), 360, 8)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
revolve_partial_union)
	# 部分角が閉多様体・キャップ向き正しい証明: box union が通る → 22v40f。
	SRAVA_SOURCE='var mNVF0 = export(box(5,5,5) ||| revolve(rect(1,2), 90)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
svg_roundtrip)
	# 2D SVG export→import round-trip。穴あき額縁が保たれて extrude=トンネル付き 16v32f。
	rm -f /tmp/srava-rt-test.svg
	SRAVA_SOURCE='export("/tmp/srava-rt-test.svg", rect(4,4) --- (rect(2,2) >>> [1,1,0])); var mNVF0 = export(extrude(import("/tmp/srava-rt-test.svg"), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
dxf_export)
	# 2D DXF export(LWPOLYLINE)。エラーにならず D_REF 出力。
	rm -f /tmp/srava-dxf-test.dxf
	SRAVA_SOURCE='export("/tmp/srava-dxf-test.dxf", ngon(6,1));' exec "$SRAVA" ;;
default_kernel_union)
	# ★manifold 既定 (module("manifold.so",{priority}) で明示・Phase4c で env DEFAULT_OUTPUT 撤去):
	#   leaf→union→volume が in-proc Manifold で動く。
	SRAVA_SOURCE='module("manifold.so",{priority:99}); print("VOL", volume(box(2,2,2) ||| box(1,1,3)));' exec "$SRAVA" ;;
default_kernel_booleans)
	# ★manifold 既定: intersection / difference。box(2,2,2) ∩ box(1,1,3) = 1x1x2 = 2 /
	# box(2,2,2) - box(1,1,3) = 8-2 = 6。
	SRAVA_SOURCE='module("manifold.so",{priority:99}); print("IVOL", volume(box(2,2,2) &&& box(1,1,3))); print("DVOL", volume(box(2,2,2) --- box(1,1,3)));' exec "$SRAVA" ;;
default_kernel_export)
	# ★manifold 既定: mesh 出力 (STL)。box union = 28 tri (Manifold 表現)。
	rm -f /tmp/srava-defk-test.stl
	SRAVA_SOURCE='module("manifold.so",{priority:99}); export("/tmp/srava-defk-test.stl", box(2,2,2) ||| box(1,1,3));' "$SRAVA" || exit 1
	python3 -c 'import struct,sys; b=open("/tmp/srava-defk-test.stl","rb").read(); n=struct.unpack_from("<I",b,80)[0]; print("TRI",n); sys.exit(0 if n==28 else 1)' ;;
default_kernel_2d_extrude)
	# ★manifold 既定: 2D (rect) → extrude → volume = 20*10*3 = 600。
	SRAVA_SOURCE='module("manifold.so",{priority:99}); print("EVOL", volume(extrude(rect(20,10), 3)));' exec "$SRAVA" ;;
default_kernel_offset3d)
	# ★manifold 既定でも 3D offset は CGAL へ自動フォールバック (mf_agent_supports から offset を
	# 除外・ひさ判断 2026-08-06)。cast 不要で動くこと + 体積が正 (拡大) であることを見る。
	SRAVA_SOURCE='module("manifold.so",{priority:99}); print("OVOL", volume(offset(box(2,2,2), 1)) > volume(box(2,2,2)));' exec "$SRAVA" ;;
default_kernel_import_obj)
	# ★manifold 既定で import(.obj) が動くこと (Phase2-2 の import_exts 対称化)。mf は STL/OFF しか
	# 読めないので .obj は CGAL に振られる。旧実装は import が拡張子未検査で mf に振られ失敗していた。
	# box(2,2,2) を .obj で書いて読み戻し volume=8 を確認。
	OBJ=/tmp/srava-defk-import.obj
	rm -f "$OBJ"
	SRAVA_SOURCE="export(\"$OBJ\", box(2,2,2));" "$SRAVA" >/dev/null 2>&1 || exit 1
	SRAVA_SOURCE="module(\"manifold.so\",{priority:99}); print(\"IVOL\", volume(import(\"$OBJ\")));" exec "$SRAVA" ;;
default_kernel_3mf)
	# ★manifold 既定: .3mf export は CGAL に振られ (mf は STL/OFF のみ)、**本物の 3MF (zip)** が
	# できること。旧実装は mf に流れて無言で STL の中身になっていた (2026-08-06 修正の回帰)。
	rm -f /tmp/srava-defk-test.3mf
	SRAVA_SOURCE='module("manifold.so",{priority:99}); export("/tmp/srava-defk-test.3mf", box(2,2,2) ||| box(1,1,3));' "$SRAVA" || exit 1
	python3 -c 'import zipfile,sys; z=zipfile.ZipFile("/tmp/srava-defk-test.3mf"); ok="3D/3dmodel.model" in z.namelist(); print("ZIP3MF", 1 if ok else 0); sys.exit(0 if ok else 1)' ;;
disable_cgal)
	# ★ module("so","off") 実行時無効化 (2026-08-10)。cgal は既定カーネル (priority 20 > manifold 10)。
	#   module("cgal.so","off") で cgal を routing 候補から外すと、leaf→union→export が次点の manifold へ
	#   落ちる。判別子 = 三角形数: cgal union = 46 tri / manifold union = 28 tri。★TRI 28 が出れば
	#   「cgal 無効化 → manifold へフォールバック」の証明 (priority override は使わない = disable の効果)。
	rm -f /tmp/srava-disable-cgal.stl
	SRAVA_SOURCE='module("cgal.so","off"); export("/tmp/srava-disable-cgal.stl", box(2,2,2) ||| box(1,1,3));' "$SRAVA" || exit 1
	python3 -c 'import struct,sys; b=open("/tmp/srava-disable-cgal.stl","rb").read(); n=struct.unpack_from("<I",b,80)[0]; print("TRI",n); sys.exit(0 if n==28 else 1)' ;;
module_off_invisible)
	# ★ #3439 ⑥: module(so,"off") が「最初からロードしなかった場合」と同じ挙動になること。
	#   ★ 2026-08-28: off は **実アンロード (dlclose)** になった。見え方の要求は同じ。
	#   旧実装は「ロードしたまま routing 候補から外す」だけで、型・4CC・codec・実行体・拡張子は
	#   登録されたまま生き続けていた (register_descriptor が無条件登録し、is_enabled を見るのは
	#   選択ループ 6 箇所だけ)。派生テーブルを全廃し記述子走査 + is_enabled にしたので、
	#   off にしたモジュールの機能は**どの層からも見えない**はず。
	#   ① 型/routing 層: その型は産出できない
	#   ② 拡張子層: cgal だけが書ける .svg は書けない (ファイルもできない)
	#   ③ off の意味は保たれる: 既定カーネルが次点 (manifold) へ落ちて計算は通る
	#   ④ module(so,{}) で再ロードできる (アンロードは不可逆でない)
	out=$(SRAVA_SOURCE='module("cgal.so","off"); print("V", volume(cast("cg-mesh3d", box(2,2,2))));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "産出できるモジュールが無い" || { echo "FAIL(1): off 中の型へ cast できてしまう: $out"; exit 1; }
	rm -f /tmp/srava-off-invisible.svg
	out=$(SRAVA_SOURCE='module("cgal.so","off"); export("/tmp/srava-off-invisible.svg", rect(2,2));' "$SRAVA" 2>&1)
	if [ -f /tmp/srava-off-invisible.svg ]; then
		echo "FAIL(2): off 中の cgal が .svg を書いた (拡張子層に off が効いていない)"; exit 1
	fi
	# ★ #3439 ⑦: 「書けるモジュールが無い」と routing 段階で言うこと (旧: 一般ロジックへ落ちて
	#   実行時に "export: no mesh to write" という的外れなエラーになっていた)。
	echo "$out" | grep -q "拡張子 'svg' を書けるモジュールが無い" ||
		{ echo "FAIL(2): .svg export のエラーが原因を指していない: $out"; exit 1; }
	out=$(SRAVA_SOURCE='module("cgal.so","off"); print("V", volume(box(2,2,2)));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "^V 8" || { echo "FAIL(3): off で次点カーネルへ落ちない: $out"; exit 1; }
	out=$(SRAVA_SOURCE='module("cgal.so","off"); module("cgal.so",{});
	      print("V", volume(cast("cg-mesh3d", box(2,2,2))));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "^V 8" || { echo "FAIL(4): 再ロードで戻せない: $out"; exit 1; }
	# ⑤ 未対応の拡張子は import/export とも routing で明示エラー (誰も扱えない形式)
	out=$(SRAVA_SOURCE='export("/tmp/srava-off-invisible.zzz", box(1,1,1));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "拡張子 'zzz' を書けるモジュールが無い" ||
		{ echo "FAIL(5): 未対応拡張子の export が明示エラーでない: $out"; exit 1; }
	out=$(SRAVA_SOURCE='print("V", volume(import("/tmp/srava-nonexistent.zzz")));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "拡張子 'zzz' を読めるモジュールが無い" ||
		{ echo "FAIL(6): 未対応拡張子の import が明示エラーでない: $out"; exit 1; }
	echo "MODULE-OFF-INVISIBLE-OK" ;;
cast_no_producer)
	# ★ #3439 ①: cast の目標型を産出できるモジュールが無いとき **明示エラー**になること。
	#   旧実装は一般 routing へフォールバックし、cast が identity として実行されて
	#   **要求した型と違う型が黙って返っていた** (誰も申告していない型名でも通っていた)。
	#   ここでは 3 点を見る:
	#     ① 存在しない型名 → エラー
	#     ② module("cgal.so","off") 下で cg 型へ cast → エラー (= off が cast の行き先にも効く)
	#     ③ 正常な cast は従来どおり通る (エラーにし過ぎていない)
	out=$(SRAVA_SOURCE='print("V", volume(cast("zz-mesh3d", box(2,2,2))));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "産出できるモジュールが無い" || { echo "FAIL(1): 存在しない型への cast が素通り: $out"; exit 1; }
	echo "$out" | grep -q "^V " && { echo "FAIL(1): 値が返っている: $out"; exit 1; }
	out=$(SRAVA_SOURCE='module("cgal.so","off"); print("V", volume(cast("cg-mesh3d", box(2,2,2))));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "産出できるモジュールが無い" || { echo "FAIL(2): off 中のモジュールの型へ cast できてしまう: $out"; exit 1; }
	out=$(SRAVA_SOURCE='print("V", volume(cast("cg-mesh3d", box(2,2,2))));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "^V 8" || { echo "FAIL(3): 正常な cast が通らない: $out"; exit 1; }
	echo "CAST-NO-PRODUCER-OK" ;;
disable_cgal_reenable)
	# ★ off (アンロード) の後、module(so,{}) で再ロードできること。再ロード後は既定 cgal に戻り TRI 46。
	#   ★ 2026-08-28: 旧 "on" は撤去した ("off" が実アンロードになった以上、戻すのは再ロード)。
	rm -f /tmp/srava-reenable-cgal.stl
	SRAVA_SOURCE='module("cgal.so","off"); module("cgal.so",{}); export("/tmp/srava-reenable-cgal.stl", box(2,2,2) ||| box(1,1,3));' "$SRAVA" || exit 1
	python3 -c 'import struct,sys; b=open("/tmp/srava-reenable-cgal.stl","rb").read(); n=struct.unpack_from("<I",b,80)[0]; print("TRI",n); sys.exit(0 if n==46 else 1)' ;;
disable_cgal_sugar)
	# ★ 1 引数 module(so) は module(so,"on") の糖衣 (2026-08-18・ひさ確定)。
	#   off の後に **1 引数**で呼んで戻せること = 既定 cgal に戻り TRI 46。
	#   (以前の 1 引数 module は「ロードし直す」op で、その副作用でロード順まで動かしていた。
	#    今は記述子の上書きだけを行い、ロード順には触れない。)
	rm -f /tmp/srava-sugar-cgal.stl
	SRAVA_SOURCE='module("cgal.so","off"); module("cgal.so"); export("/tmp/srava-sugar-cgal.stl", box(2,2,2) ||| box(1,1,3));' "$SRAVA" || exit 1
	python3 -c 'import struct,sys; b=open("/tmp/srava-sugar-cgal.stl","rb").read(); n=struct.unpack_from("<I",b,80)[0]; print("TRI",n); sys.exit(0 if n==46 else 1)' ;;
cast_sig_input)
	# ★ 2026-08-28 (ひさ指摘): cast の routing は sig の **出力型だけ** を見ていたので、
	#   sig が「受けられない」と申告している型が入力に来ても通していた。入力型も照合するようにした。
	#   ① 申告済みの変換 (cg-mesh3d → mf-mesh3d) は通る
	#   ② 申告に無い入力型 (d4-mesh3d → mf-mesh3d) は **planner 段で** 明示エラー。
	#      agent を起こしてからの codec エラー ("cannot convert format") ではないことを見る
	#      = この検査を外すと ② は codec エラーになるので、メッセージで判別する。
	#   ③ エラーは入力の **型名** を出す。**形式 (4CC) には踏み込まない** — in-proc の値はまだ
	#      メモリ上の body でしかなく、4CC は pigDataCache の都合なので planner は知る立場にない。
	out=$(SRAVA_SOURCE='module("cgal.so",{}); module("manifold.so",{});
	      var a = cast("cg-mesh3d", box(2,2,2)); print("V-cg", volume(a));
	      print("V-mf", volume(cast("mf-mesh3d", a)));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "^V-mf 8" || { echo "FAIL(1): 申告済みの cg->mf cast が通らない: $out"; exit 1; }
	out=$(SRAVA_SOURCE='module("d4.so",{}); module("manifold.so",{});
	      var a = d4_cube(2); print("N", d4_nfaces(a));
	      print("V-mf", volume(cast("mf-mesh3d", a)));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "^N 12" || { echo "FAIL(2): d4 の leaf が作れていない: $out"; exit 1; }
	echo "$out" | grep -q "no module declares a conversion" ||
		{ echo "FAIL(3): 申告に無い入力型が routing で弾かれていない: $out"; exit 1; }
	echo "$out" | grep -q "d4-mesh3d" ||
		{ echo "FAIL(4): エラーが入力の型名を示していない: $out"; exit 1; }
	# ★ planner の routing は **形式 (4CC) に踏み込まない** (in-proc の値はまだメモリ上の body で
	#   しかない = 4CC は pigDataCache の都合)。形式を出すのは読む側だけ。
	echo "$out" | grep -q "format '" &&
		{ echo "FAIL(4b): routing のエラーが形式 (4CC) に踏み込んでいる: $out"; exit 1; }
	echo "$out" | grep -q "^V-mf " && { echo "FAIL(5): エラーなのに値が返っている: $out"; exit 1; }
	echo "CAST-SIG-INPUT-OK" ;;
module_info)
	# ★ 2026-08-28 (ひさ要望): srava --module-info = 記述子の申告ダンプ (op ごとの sig 全リスト・
	#   codec の型名登録簿・wires の能力)。--modules (配置の問い) とは別コマンド。
	#   ★ 分けた理由は出力量 — 混ぜると 37 行の配置表が 500 行超の申告に埋もれる。
	MD=$(dirname "$SRAVA")
	out=$(SRAVA_MODULE_PATH="$MD" "$SRAVA" --module-info manifold 2>&1)
	echo "$out" | grep -q "^manifold  (abi=" || { echo "FAIL(1): 見出しが出ない: $out"; exit 1; }
	echo "$out" | grep -q "sig = \[mf-mesh3d\](\*)->mf-mesh3d" ||
		{ echo "FAIL(2): op の sig が出ない: $out"; exit 1; }
	# ★ 実行方式は caps (できること) と default (既定) の 2 つを出す — 別物なので両方要る
	#   (openvdb は caps=thread|process だが default=process)。
	echo "$out" | grep -q "exec_caps=thread|process(0x3)  exec_default=thread  make_agent=yes" ||
		{ echo "FAIL(2b): exec_caps / exec_default / make_agent が出ない: $out"; exit 1; }
	echo "$out" | grep -q "^      mfGeom .*types = mf-mesh3d,mf-cross2d" ||
		{ echo "FAIL(3): 階層名と型名が 1 行で出ない: $out"; exit 1; }
	echo "$out" | grep -q "create=yes reader=yes writer=yes match=yes" ||
		{ echo "FAIL(4): 階層の能力が出ない: $out"; exit 1; }
	# ★ tags は **診断専用の申告**なので、実際に create へ通して検証する (ずれたら表に出る)。
	echo "$out" | grep -q "tag 'MFM3' -> mf-mesh3d" ||
		{ echo "FAIL(4b): tag のプローブ結果が出ない: $out"; exit 1; }
	echo "$out" | grep -q "NOT accepted" &&
		{ echo "FAIL(4c): 申告した tag を create が受理しない: $out"; exit 1; }
	# 絞り込みが効く (他のモジュールが混ざらない)
	echo "$out" | grep -q "^cgal  (abi=" && { echo "FAIL(5): 名前で絞れていない: $out"; exit 1; }
	# 名前を間違えたら明示的に言う (黙って空で終わらない)
	out=$(SRAVA_MODULE_PATH="$MD" "$SRAVA" --module-info nosuchmodule 2>&1)
	echo "$out" | grep -q "no such module is loaded" ||
		{ echo "FAIL(6): 存在しない名前が黙って通る: $out"; exit 1; }
	# --modules は配置の問いのまま (申告で埋もれない = sig 行が出ない)
	out=$(SRAVA_MODULE_PATH="$MD" "$SRAVA" --modules 2>&1)
	echo "$out" | grep -q "sig = " && { echo "FAIL(7): --modules に申告が混ざった: $out"; exit 1; }
	echo "$out" | grep -q "^loaded:" || { echo "FAIL(7): --modules の loaded 節が無い: $out"; exit 1; }
	echo "MODULE-INFO-OK" ;;

module_unload_reload)
	# ⚠ harness が消すのは $D だけなので、検査ごとの $D-N は自分で消す。残っていると
	#   ④ の volume が前回実行の結果に HIT して agent が起動せず、空振りする。
	rm -rf "$D"-1 "$D"-2 "$D"-3 "$D"-4 "$D"-5
	# ★ 2026-08-28 (ひさ設計): module(so,"off") は **実アンロード (dlclose)**。
	#   ① 未ロードへの off は明示エラー (名前を間違えたら気づけるように)
	#   ② module_loaded で載っているか判定できる (ロードという副作用は持たない)
	#   ③ off → module(so,{}) で再ロードできる
	#   ④ **一度使ったモジュールは落とせない** (その .so 由来の本体/agent が生きうるため)
	#   ⑤ lib/module/reload.sra の module_reload が ①〜③ を包む
	#   ⚠ この harness は SRAVA_MODULE_ALL=1 を全ケースに効かせている (= 実カーネル一式が
	#     ロード済みで始まる)。未ロード状態を見たいケースだけ 0 に落とす。
	#   ⚠ 検査ごとに **キャッシュを分ける**。共有すると後の検査が前の結果に HIT して agent が
	#     起動せず、「そのモジュールを使っていない」状態になってしまう (④ が空振りする)。
	out=$(SRAVA_CACHE_DIR="$D-1" SRAVA_MODULE_ALL=0 SRAVA_SOURCE='module("cgal.so","off"); print("X",1);' "$SRAVA" 2>&1)
	echo "$out" | grep -q "module is not loaded" || { echo "FAIL(1): 未ロードへの off がエラーでない: $out"; exit 1; }
	echo "$out" | grep -q "^X 1" && { echo "FAIL(1b): エラーなのに先へ進んだ: $out"; exit 1; }

	out=$(SRAVA_CACHE_DIR="$D-2" SRAVA_MODULE_ALL=0 SRAVA_SOURCE='print("A", module_loaded("cgal.so"));
	      module("cgal.so",{}); print("B", module_loaded("cgal.so"));
	      module("cgal.so","off"); print("C", module_loaded("cgal.so"));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "^A 0" || { echo "FAIL(2): 未ロードが 0 でない: $out"; exit 1; }
	echo "$out" | grep -q "^B 1" || { echo "FAIL(2): ロード後が 1 でない: $out"; exit 1; }
	echo "$out" | grep -q "^C 0" || { echo "FAIL(2): アンロード後が 0 でない (dlclose されていない): $out"; exit 1; }

	# ③ 落として読み直せる (再ロード後も計算が通る)
	out=$(SRAVA_CACHE_DIR="$D-3" SRAVA_MODULE_ALL=0 SRAVA_SOURCE='module("cgal.so",{}); module("cgal.so","off"); module("cgal.so",{});
	      print("V", volume(box(2,2,2)));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "^V 8" || { echo "FAIL(3): 再ロード後に計算できない: $out"; exit 1; }

	# ④ 使った後は落とせない (明示エラー・黙って落として後で落ちる、にしない)
	out=$(SRAVA_CACHE_DIR="$D-4" SRAVA_MODULE_ALL=0 SRAVA_SOURCE='module("cgal.so",{}); print("V", volume(box(2,2,2)));
	      module("cgal.so","off"); print("X",1);' "$SRAVA" 2>&1)
	echo "$out" | grep -q "already used by this program" ||
		{ echo "FAIL(4): 使用済みモジュールが落とせてしまう: $out"; exit 1; }
	echo "$out" | grep -q "^X 1" && { echo "FAIL(4b): エラーなのに先へ進んだ: $out"; exit 1; }

	# ⑤ module_reload (未ロードでも既ロードでも通る)
	out=$(SRAVA_CACHE_DIR="$D-5" SRAVA_MODULE_ALL=0 SRAVA_SOURCE='include "module/reload.sra";
	      module_reload("cgal.so", {priority:99});
	      module_reload("cgal.so", {priority:98});
	      print("L", module_loaded("cgal.so")); print("V", volume(box(2,2,2)));' "$SRAVA" 2>&1)
	echo "$out" | grep -q "^L 1" || { echo "FAIL(5): module_reload 後に載っていない: $out"; exit 1; }
	echo "$out" | grep -q "^V 8" || { echo "FAIL(5): module_reload 後に計算できない: $out"; exit 1; }
	echo "MODULE-UNLOAD-RELOAD-OK" ;;

module_dup_name)
	# ★ 2026-08-28 (ひさ指摘): 同じファイル名で **別の実ファイル** を module() したら明示エラー。
	#   旧実装は load_file がファイル名だけで 既ロード判定をしていたため、パスを明示しても
	#   **黙って no-op** になり、ロードしたつもりで別の .so が動いていた。
	#   不変条件「1 モジュール名につき dlopen は 1 回」(#3425) は保ったまま、
	#   衝突を黙らせずに言う。同じ実体を指す再指定は冪等のまま (エラーにし過ぎない)。
	MD="$T/srava-dupname"; rm -rf "$MD"; mkdir -p "$MD"
	SODIR=$(dirname "$SRAVA")
	# 拡張子は OS 依存 (Linux/macOS=.so / MinGW・Cygwin=.dll)。実物を見て決める。
	SOEXT=.so; [ -f "$SODIR/demo.so" ] || SOEXT=.dll
	SO="$SODIR/demo$SOEXT"
	cp "$SO" "$MD/demo$SOEXT" || { echo "FAIL(0): cannot copy $SO"; exit 1; }
	# ① 同名別ファイル → エラー
	out=$(SRAVA_SOURCE="module(\"demo$SOEXT\",{}); module(\"$MD/demo$SOEXT\",{}); print(\"X\",1);" "$SRAVA" 2>&1)
	echo "$out" | grep -q "already loaded from" || { echo "FAIL(1): 同名別ファイルが黙って通る: $out"; exit 1; }
	echo "$out" | grep -q "^X 1" && { echo "FAIL(1): エラーなのに先へ進んだ: $out"; exit 1; }
	# ② optional:1 でも飲み込まない (optional の意味は「入っていない」だけ)
	out=$(SRAVA_SOURCE="module(\"demo$SOEXT\",{}); module(\"$MD/demo$SOEXT\",{optional:1}); print(\"X\",1);" "$SRAVA" 2>&1)
	echo "$out" | grep -q "already loaded from" || { echo "FAIL(2): optional:1 が衝突を飲み込んだ: $out"; exit 1; }
	# ③ 同じ名を 2 度 → 冪等 (エラーにし過ぎていないこと)
	out=$(SRAVA_SOURCE="module(\"demo$SOEXT\",{}); module(\"demo$SOEXT\",{}); print(\"X\",1);" "$SRAVA" 2>&1)
	echo "$out" | grep -q "^X 1" || { echo "FAIL(3): 同じモジュールの再指定がエラーになった: $out"; exit 1; }
	# ④ 同じ実体を **別のパス文字列** で指す → エラーにしない (dev,ino 判定)。
	#   symlink が使えない環境 (Windows) ではここは飛ばす。
	if ln -s "$SO" "$MD/link$SOEXT" 2>/dev/null; then
		out=$(SRAVA_SOURCE="module(\"demo$SOEXT\",{}); module(\"$MD/link$SOEXT\",{}); print(\"X\",1);" "$SRAVA" 2>&1)
		echo "$out" | grep -q "already loaded from" && { echo "FAIL(4): 同じ実体を別物と誤判定: $out"; exit 1; }
	fi
	echo "MODULE-DUP-NAME-OK" ;;
load_op_removed)
	# ★ 旧 load(so) op は廃止 (2026-08-18)。module(so) が同じ役割を兼ねるため。
	#   黙って別の意味にならず、エラーになることを固定する。
	SRAVA_SOURCE='load("d3.so"); print("X", 1);' exec "$SRAVA" ;;
disable_bad_option)
	# ★ 不正な文字列オプションは明示エラー ("off" 以外は無い)。
	SRAVA_SOURCE='module("cgal.so","nope"); print("X", 1);' exec "$SRAVA" ;;
kernel_mix_cast)
	# ★カーネル混成 (module("manifold.so",{priority}) で manifold 既定に): mf が書いた MFM3 を
	# cast で cg agent が読む = MFM3→EPECK 昇格読みの回帰 (#3404 の昇格が #3406 の
	# codec テーブル移行で不通になっていた実バグ・2026-08-06 cgCacheCodecUpgrade で再接続)。
	# rev4 Phase C: cast は目標**型**指定 (旧 cast("exact") → cast("cg-mesh3d"))。
	SRAVA_SOURCE='module("manifold.so",{priority:99}); print("VOL", volume(cast("cg-mesh3d", box(2,2,2) ||| box(1,1,3))));' exec "$SRAVA" ;;
kernel_mix_cast_downgrade)
	# ★ cg→mf downgrade の回帰 (2026-08-12 修正): 既定 cgal で作った MESH を cast("mf-mesh3d",…) で
	#   manifold が読む (mf_codecs の mf-cg-downgrade codec が MESH→mf-mesh3d を decode_mesh_exact で
	#   double 化)。以前は "cast: needs a mesh" で失敗していた。3D のみ (2D PLY2 は未対応)。
	SRAVA_SOURCE='print("VOL", volume(cast("mf-mesh3d", box(2,2,2) ||| box(1,1,3))));' exec "$SRAVA" ;;
kernel_mix_cast_downgrade_leaf)
	# ★ leaf 入力 × cross-module 変換の COLD 回帰 (2026-08-12 修正): computed (union) と違い
	#   leaf (box 直) は生産者が速く、A_SAVE_BEGIN で解決された outCache ハンドルを消費者が即読む。
	#   leaf 生産者の ACT_START HIT 判定が焼き込んだ CV_INVALID を A_SAVE_BEGIN の mark_valid が
	#   癒さないと「cache not valid and no writer」で panic した。★cold 必須 → cache dir を毎回消す。
	rm -rf "$SRAVA_CACHE_DIR"
	SRAVA_SOURCE='print("VOL", volume(cast("mf-mesh3d", box(2,2,2))));' exec "$SRAVA" ;;
kernel_mix_cast_downgrade_2d)
	# ★ 2D downgrade (PLY2→mf-cross2d・2026-08-12 実装) + leaf cold の複合回帰。★cold 必須 (同上)。
	rm -rf "$SRAVA_CACHE_DIR"
	SRAVA_SOURCE='print("AREA", area(cast("mf-cross2d", rect(4,3))));' exec "$SRAVA" ;;
kernel_mix_dxf)
	# ★カーネル混成: mf の 2D (MFC2) を .dxf export (CGAL 固定) が読む = MFC2→Pwh 昇格読みの回帰。
	rm -f /tmp/srava-kmix-test.dxf
	SRAVA_SOURCE='module("manifold.so",{priority:99}); export("/tmp/srava-kmix-test.dxf", offset(rect(20,10), 2));' exec "$SRAVA" ;;
kernel_mix_cgalonly)
	# ★ choice A (2026-08-10・sig 化): cgal 専用 op (manifold が持たない) に **mf mesh** を渡すと、
	#   decide_executor が cgal の foreign sig ((mf-…)->…) で直接一致させ cgal へ振り、cgal が昇格読みして実行。
	#   旧 coercion を明示 sig 化した後も、この暗黙クロスカーネルが維持されることの回帰。
	#   perimeter (2D cgal 専用・rect は mf)・repair (3D cgal 専用・box は mf) を mf 入力で。
	OUT=$(SRAVA_SOURCE='module("manifold.so",{priority:99});
	var ok = 0;
	if (perimeter(rect(4,3)) > 13) { if (volume(repair(box(2,2,2))) > 7) { ok = 1; } }
	print("CGONLY", ok);' "$SRAVA" 2>&1 | grep "^CGONLY")
	if [ "$OUT" = "CGONLY 1" ]; then echo "CGONLY_OK"; else echo "CGONLY_FAIL: $OUT"; fi ;;
dxf_roundtrip)
	# DXF export→import round-trip。穴あき額縁が包含 nest で復元 → extrude トンネル付き 16v32f。
	rm -f /tmp/srava-rt-test.dxf
	SRAVA_SOURCE='export("/tmp/srava-rt-test.dxf", rect(4,4) --- (rect(2,2) >>> [1,1,0])); var mNVF0 = export(extrude(import("/tmp/srava-rt-test.dxf"), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
offset_inset)
	# 2D インセット(straight skeleton): rect(4,4) を -1 収縮 → 2x2 相当 → extrude 8v12f。
	SRAVA_SOURCE='var mNVF0 = export(extrude(offset(rect(4,4), -1), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
offset_shell)
	# 肉厚枠: offset(-1) を引いて幅1の枠 → 穴あき → extrude トンネル付き 16v32f。
	SRAVA_SOURCE='var mNVF0 = export(extrude(rect(4,4) --- offset(rect(4,4), -1), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
offset_vanish)
	# インセット過大 → 領域消滅(空)。extrude すると空メッシュ 0v0f。
	SRAVA_SOURCE='var mNVF0 = export(extrude(offset(rect(2,2), -5), 1)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
# ★ 3D offset のテストは **nef へ振り替えた** (#3440 の 2: test/srava_nef.sh の offset モード)。
#   cgal.so の 3D offset は中身が Nef + 凸分解でモジュール境界の約束①違反だったため移設。
#   2D offset (straight skeleton) は cgal に残るので上の offset / offset_vanish はここに健在。
syntaxerr)
	SRAVA_SOURCE='export(box(2,2,2))' exec "$SRAVA" ;;   # ; 欠落 → parse error
length)
	# length(array)=4 / length(hash)=3 を print で観測(planner 側 op・agent 不要)。
	SRAVA_SOURCE='print(length([1,2,3,4])); print(length({a:1, b:2, c:3}));' exec "$SRAVA" ;;
line_guide)
	# 2D ガイド line: 部品(塗り)に寸法線(ストローク)を +++ で重ね SVG 出力。
	# 配列形式 line([[..],[..]]) と 2 引数形式 line(p0,p1) の両方を使い、塗り <path> と
	# ガイド <polyline>(2 本)が出ることを確認。
	EF="$D.svg"
	SRAVA_SOURCE='export("'$EF'", rect(20,10) +++ line([[0,-3],[20,-3]]) +++ line([0,0],[10,12]), "mm");' "$SRAVA" >/dev/null 2>&1
	NP=$(grep -c "<polyline" "$EF" 2>/dev/null)
	if [ "$NP" = "2" ] && grep -q "<path" "$EF"; then echo "LINE_GUIDE_OK"; else echo "LINE_GUIDE_FAIL"; fi ;;
concat)
	# concat: 配列連結(planner 側 op)。配列は要素展開、非配列は 1 要素追加。length で観測。
	SRAVA_SOURCE='print(length(concat([1,2,3],[4,5],6)));' exec "$SRAVA" ;;
prism_axis)
	# prism/pyramid は Z 軸(高さ)に統一 → prism(n,h,r) ≡ extrude(ngon(n,r),h)。体積一致を検証。
	SRAVA_SOURCE='print("PEQ=", volume(prism(6,8,2)) == volume(extrude(ngon(6,2),8)));' exec "$SRAVA" ;;
section)
	# 3D→2D 断面: 中空箱を z=5 で水平に切る → 外周 10x10 − 穴 6x6 = area 64(even-odd で穴検出)。
	# section(m,P,N) は 3 要素配列 [ε=0, ε−, ε+]。共面でないので [0] が答え・[1][2] は空集合。
	# 4 引数形 section(m,P,N,0) は単一の断面(移行と使い分け用)。両方が 64 で一致することも見る。
	SRAVA_SOURCE='var hollow = box(10,10,10) --- (box(6,6,12) >>> [2,2,-1]);
	var s = section(hollow, [0,0,5], [0,0,1]);
	print("SECAREA=", area(s[0]) + area(s[1])*1000 + area(s[2])*1000
	                + (area(section(hollow, [0,0,5], [0,0,1], 0)) - 64)*1000);' exec "$SRAVA" ;;
section_coplanar)
	# 共面(平面が面にちょうど乗る)ケース: 箱の上面 z=2 で切る。
	#   [0] = 空(平面ちょうどは退化=定義できない・共面ありの合図)
	#   [1] = 直下の極限 = 2x2 の断面 = 4
	#   [2] = 直上の極限 = 何もない = 0
	# 旧実装(slicer + 弦で閉じる)はここでキメラ断面を返していた。
	SRAVA_SOURCE='var t = section(box(2,2,2), [1,1,2], [0,0,1]);
	print("COPL=", area(t[0])*100 + area(t[1])*10 + area(t[2]));' exec "$SRAVA" ;;
empty_set)
	# empty2d()/empty3d() = 値としての空集合。{}(fold の中立元)とは別物であることを見る:
	#   intersection(a, empty3d()) = 空(0) / intersection(a, {}) = a(8)
	SRAVA_SOURCE='var B = box(2,2,2);
	print("EMPTY=", volume(intersection(B, empty3d()))*100 + volume(union(B, empty3d()))*10
	              + volume(intersection(B, {})) + area(empty2d())*1000);' exec "$SRAVA" ;;
control_flow)
	# return / break / continue。f(5)=1(return)、s=12(for+continue で step が走る)、
	# t=10(while+break)。合計検証 f(5)*100 + s + t = 122。continue が step を飛ばすと TIMEOUT。
	SRAVA_SOURCE='var f=\(x){ if(x>0){return 1;} return -1; };
	var s=0; var i; for(i=0;i<6;i=i+1){ if(i==3){continue;} s=s+i; }
	var t=0; var j=0; while(j<1000){ if(j==5){break;} t=t+j; j=j+1; }
	print("CF=", f(5)*100 + s + t);' exec "$SRAVA" ;;
idx_assign)
	# 添字/メンバ代入: 空配列をループで成長、ネスト、ハッシュメンバ。
	# s=[0,1,4,9], m[0][1]=9, h.b=2 → 全部効けば "9 9 2" 相当。print で観測。
	SRAVA_SOURCE='var s=[]; var i; for(i=0;i<4;i=i+1){ s[i]=i*i; } var m=[[0,0]]; m[0][1]=9; var h={a:1}; h.b=2; print(s); print(m); print(h.b);' exec "$SRAVA" ;;
arr_arith)
	# 配列の要素ごと算術: +/- は配列同士、* / はスカラーブロードキャスト。
	# ([1,2]+[3,4])*2 - [1,1] = [4,6]*2 - [1,1] = [8,12]-[1,1] = [7,11]
	SRAVA_SOURCE='print(([1,2] + [3,4]) * 2 - [1,1]);' exec "$SRAVA" ;;
printval)
	# print: 値(int/string/float)をそのまま stdout に表示。
	SRAVA_SOURCE='print(42); print("hello"); print(1.5);' exec "$SRAVA" ;;
printmesh)
	# print(mesh): 継続を辿り agent 完了後に pigDataCache のハッシュファイル名(.cache パス)を表示。
	SRAVA_SOURCE='print(box(1,1,1));' exec "$SRAVA" ;;
combine_op)
	# +++ 演算子: 交差を解かず 2 箱を単純合体(viewer 用)= 16v24f(2 連結成分)。
	SRAVA_SOURCE='var mNVF0 = export(box(2,2,2) +++ box(1,1,3)); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
combine_fn)
	# combine(a,b,c): n-ary も二項分解で合体。3 箱 = 24v36f。
	SRAVA_SOURCE='var mNVF0 = export(combine(box(2,2,2), box(1,1,3), box(3,1,1))); print("NVF", nverts(mNVF0), nfaces(mNVF0));' exec "$SRAVA" ;;
rect_neg)
	# 負の幅 rect は退化ポリゴン → 2D union でエージェントがクラッシュしていた回帰。
	# 今は rect が明示エラー(位置付き)で弾く。クラッシュ(agent closed)しないことを確認。
	SRAVA_SOURCE='export("'$D'/x.svg", rect(260,135) ||| rect(-34,145), "mm");' "$SRAVA" 2>&1 \
	  | grep -E 'rect: width and height must be positive' | head -1 ;;
selfint_err)
	# 接して(tangent)非多様体化した中間結果を次の boolean に渡すと CGAL が segfault していた回帰。
	# throw_on_self_intersection + is_closed ゲートでクラッシュせず明示エラーになることを担保。
	# box(x[10,30]) と prism(半径10=x[-10,10]) が x=10 で接触 → 自己交差 → 次の |||sphere で従来クラッシュ。
	SRAVA_SOURCE='var pitch=32;
	export("'$D'/o.stl", box(20,13.5,30)>>>[10,0,0] ||| (prism(pitch,30,10)>>>[0,13.5,0]) ||| (sphere(10,pitch)>>>[0,13.5,0]));' "$SRAVA" 2>&1 | grep -E 'boolean failed' | head -1 ;;
coplanar_err)
	# 3D boolean が同一平面の一致で非多様体になる場合、黙って空を返さず明確にエラーにする。
	# shell を上面 coplanar な box で引く → "boolean failed" エラー。
	SRAVA_SOURCE='var T=1.5;var H=50;var TM=10;
	var cover = extrude(rect(13,13)>>>[-T,-T] --- rect(10,10), 2*TM)>>>[0,0,H-TM];
	var base = (box(13,13,H+T)>>>[-T,-T,-T]) --- cover --- (box(10-2*T,10-2*T,H+TM)>>>[T,T,0]);
	export("'$D'/o.stl", base --- box(10,10,H-TM));' "$SRAVA" 2>&1 | grep -E 'boolean failed' | head -1 ;;
arrerr_prop)
	# 配列リテラルの要素がエラー(キー誤り等)のとき、agent の "inline arg parse error" に化けず
	# 本当の原因(hash key not found)が位置付きで出ることを検証。
	EF="$D.sra"
	printf 'var h = {height:5};\nexport("%s/x.off",\n  box(1,1,1) >>> [0, h.hight, 0]);\n' "$D" > "$EF"
	"$SRAVA" "$EF" 2>&1 | grep -E 'hash key not found: "hight"' | head -1 ;;
idxerrloc)
	# 範囲外添字・未定義変数のエラーが ERROR[file,line] で位置付き(varref/index に位置を刻む)。
	EF="$D.sra"
	printf 'var s = [0,0];\nvar i;\nfor (i=0;i<2;i=i+1){ s[i]=i; }\nprint(s[i]);\n' > "$EF"
	# ★ #3452: SRAVA_MODULE_ALL=1 の include "module/all.sra"; 合成でソース先頭に 1 行増えるため
	#   実際の行(4行目)は 5 行目として報告される。
	"$SRAVA" "$EF" 2>&1 | grep -E 'ERROR\[.*,5\] array index out of range' | head -1 ;;
errloc)
	# エラーの ERROR[file,line] 表示 + エラー時はキャッシュ掃除をしない(Feature1/2)。
	# 3 行目の volume(2D) がエラー。ファイル名と行番号、cleanup スキップを検証。
	# ★ #3452: SRAVA_MODULE_ALL=1 の include 合成で +1 行ずれる(idxerrloc と同じ理由)。
	EF="$D.sra"
	printf '// comment line 1\nvar a = box(1,1,1);\nexport(volume(rect(2,2)));\n' > "$EF"
	"$SRAVA" "$EF" 2>&1 | grep -E 'ERROR\[.*,4\]|exit cleanup: skipped' | head -2 ;;
logic)
	# 論理演算子 && || ! と優先順位。&&>||(prec1)、比較>&&(prec0)、!>==(notp)。
	# 値返し op(valid/volume)を論理オペランドにも使える。出力 "L 1 0 1 0 1 0 1 0 1 1"。
	SRAVA_SOURCE='var m = box(2,2,2) ||| box(1,1,3);
	print("L",
	  1 && 1, 1 && 0,            // 1 0
	  0 || 3, 0 || 0,            // 1 0
	  !0, !5,                    // 1 0
	  1 || 0 && 0,               // 1  (&& binds tighter: 1||(0&&0))
	  2 > 1 && 3 > 5,            // 0  (cmp binds tighter than &&)
	  !1 == 0,                   // 1  (! binds tighter than ==: (!1)==0)
	  valid(m) && (volume(m) > 5));  // 1' exec "$SRAVA" ;;
identity)
	# fold 単位元 {}(空ハッシュ・型分離): union/intersection を if(i==0) なしで畳む。a---{}=a。valid({})=0。
	# u: 3 つの離れた箱の union = 24。s: 3 つの 10 立方の積 = 800。box---{} = 8。valid({})=0。
	# 出力 "I 24 800 8 0"。
	SRAVA_SOURCE='var u = {}; var s = {}; var i;
	for ( i = 0 ; i < 3 ; i = i + 1 ) { u = u ||| box(2,2,2) >>> [i*3,0,0]; }
	for ( i = 0 ; i < 3 ; i = i + 1 ) { s = s &&& box(10,10,10) >>> [i,0,0]; }
	print("I", volume(u), volume(s), volume(box(2,2,2) --- {}), valid({}));' exec "$SRAVA" ;;
xformbcast)
	# transform 演算子の配列対応: broadcast / instancing / zip / 単一(従来)。"X 3 3 2 8"
	SRAVA_SOURCE='var arr = [box(1,1,1),box(1,1,1),box(1,1,1)];
	print("X",
	  length(arr >>> [0,0,5]),
	  volume(union(box(1,1,1) >>> [[0,0,0],[10,0,0],[0,10,0]])),
	  volume(union([box(1,1,1),box(1,1,1)] >>> [[0,0,0],[20,0,0]])),
	  volume(box(2,2,2) >>> [1,1,1]));' exec "$SRAVA" ;;
curvelib)
	# std/curve.sra(arc/bezier/spline/clothoid)。polygon に通して指数表記座標の round-trip も検証。
	# arc 17点 / bezier 11点 / 扇形の面積>0=1。"CU 17 11 1"
	SRAVA_SOURCE='include "std/curve.sra";
	var s = polygon(concat(arc(0,0,5,0,1.5707963,12), [[0,0]]));
	print("CU", length(arc(0,0,5,0,PI,16)), length(bezier([[0,0],[0,10],[10,10],[10,0]],10)), area(s) > 0);' exec "$SRAVA" ;;
arrayops)
	# transpose / cumsum / sum(planner 側・curve の土台)。"AO [[0,10],[1,11],[2,12]] [1,3,6,10] 10"
	SRAVA_SOURCE='print("AO", transpose([[0,1,2],[10,11,12]]), cumsum([1,2,3,4]), sum([1,2,3,4]));' exec "$SRAVA" ;;
mathfn)
	# 初等関数(カーネル・ベクトル化・ラジアン)。"MA 4 256 3 7 [2,3]"
	SRAVA_SOURCE='print("MA", sqrt(16.0), pow(2.0,8.0), floor(3.9), max(2.0,7.0), sqrt([4.0,9.0]));' exec "$SRAVA" ;;
mathlib)
	# std/math.sra(range/linspace/PI)。"ML [0,1,2,3] [0,2,4,6]"
	SRAVA_SOURCE='include "std/math.sra";
	print("ML", range(4), linspace(0.0,6.0,4));' exec "$SRAVA" ;;
layout)
	# stdlib(std/layout.sra)を include して row/grid を使う(SRAVA_PATH は CMake が repo/lib に設定)。
	# row(parts,1) は重ならない → vol = 8+64+1 = 73。grid 3要素。2D row の面積 4+9=13。"LAY 73 3 13"。
	SRAVA_SOURCE='include "std/layout.sra";
	var parts = [box(2,2,2), box(4,4,4), box(1,1,1)];
	print("LAY",
	  volume(union(row(parts,1))),
	  length(grid(parts,2,1)),
	  area(union(row([rect(2,2),rect(3,3)],1))));' exec "$SRAVA" ;;
includetest)
	# include "path": 字句インクルード(相対解決 + 多重 include 防止 + 定義の可視化)。
	D2="$D.inc"; rm -rf "$D2"; mkdir -p "$D2"
	printf 'var dbl = \\(x){ x*2; };\n' > "$D2/lib.sra"
	printf 'include "lib.sra";\ninclude "lib.sra";\nprint("INC", dbl(21));\n' > "$D2/main.sra"
	"$SRAVA" "$D2/main.sra" 2>&1 | grep -E 'INC 42' | head -1 ;;
includeerr)
	# 存在しない include は明示エラー。
	D2="$D.incerr"; rm -rf "$D2"; mkdir -p "$D2"
	printf 'include "nope.sra";\n' > "$D2/main.sra"
	"$SRAVA" "$D2/main.sra" 2>&1 | grep -E 'include: cannot find' | head -1 ;;
maptest)
	# map(array, fn): 1引数 \(m){…} と 2引数 \(m,i){…}。インスタンス化(map+union)も。
	# 出力 "M [1,8,27] [10,120,230] 3"。
	SRAVA_SOURCE='print("M",
	  map([box(1,1,1),box(2,2,2),box(3,3,3)], \(m){ volume(m); }),
	  map([10,20,30], \(p,i){ p + i*100; }),
	  volume(union(map([[0,0,0],[5,0,0],[0,5,0]], \(p){ box(1,1,1) >>> p; }))));' exec "$SRAVA" ;;
arrayfold)
	# union(配列): concat で集めた mesh 配列を eval 時に均衡二分木で一気に union(並列・直列 fold 回避)。
	# 4 つの離れた箱 → vol 32。union(単一 mesh)=その mesh(vol 27)。union([])={}(valid 0)。"AF 32 27 0"。
	SRAVA_SOURCE='var a = [];
	a = concat(a, box(2,2,2));
	a = concat(a, box(2,2,2) >>> [5,0,0]);
	a = concat(a, box(2,2,2) >>> [0,5,0]);
	a = concat(a, box(2,2,2) >>> [5,5,0]);
	print("AF", volume(union(a)), volume(union(box(3,3,3))), valid(union([])));' exec "$SRAVA" ;;
asyncexport)
	# export_async(非ブロッキング書き出し) + flush(明示バリア)。flush 後の system がファイルを観測可能。
	# a.stl(export_async→flush で完成)、b.copy(flush 後の system で複製)が両方できれば OK。
	rm -f "$T/srava-ae-a.stl" "$T/srava-ae-b.copy"
	SRAVA_SOURCE="export_async(\"$T/srava-ae-a.stl\", box(2,2,2));
	flush();
	system(\"cp $T/srava-ae-a.stl $T/srava-ae-b.copy\");" "$SRAVA" >/dev/null 2>&1
	if test -f "$T/srava-ae-a.stl" && test -f "$T/srava-ae-b.copy"; then echo "ASYNC_OK"; else echo "ASYNC_FAIL"; fi ;;
async)
	# async 文(統一プリミティブ)。複数 async は並列に走るが、各 sync 文は出現順に整列する。
	# body の var は同じスコープなので sync 文から参照できる(a=10, b=20)。sync 無し async(body3)も
	# チェーンに参加し、後続 sync の順序を崩さない。出力 "S 1 10","S 2 20","S 3 30" がこの順なら OK。
	OUT=$(SRAVA_SOURCE='async { var a=10; sync: print("S", 1, a); }
	async { var b=20; sync: print("S", 2, b); }
	async { print("body3"); }
	async { sync: print("S", 3, 30); }' "$SRAVA" 2>/dev/null | grep "^S ")
	EXP=$(printf 'S 1 10\nS 2 20\nS 3 30')
	if [ "$OUT" = "$EXP" ]; then echo "ASYNCSYNC_OK"; else echo "ASYNCSYNC_FAIL: [$OUT]"; fi ;;
destructure)
	# 分割代入 `var [a,b,…] = 式;` — 右辺(配列)の要素 0,1,… を各名前へ束縛する。
	#  (1) 基本: [1,2] → a=1,b=2   (2) 余りは無視: [1,2,3] を 2 名で受ける
	#  (3) 不足はエラー   (4) 右辺が配列でなければエラー
	#  (5) ★ 1 文なので並列: [volume(..),volume(..)] を同時に起動して束縛できる
	O1=$(SRAVA_SOURCE='var [a,b] = [1,2]; print("D", a, b);' "$SRAVA" 2>/dev/null | grep "^D ")
	O2=$(SRAVA_SOURCE='var [a,b] = [1,2,3]; print("D", a, b);' "$SRAVA" 2>/dev/null | grep "^D ")
	O3=$(SRAVA_SOURCE='var [a,b,c] = [1,2]; print("X");' "$SRAVA" 2>&1 | grep -c "destructuring: need 3")
	O4=$(SRAVA_SOURCE='var [a] = 5; print("X");' "$SRAVA" 2>&1 | grep -c "needs an array")
	O5=$(SRAVA_SOURCE='var [x,y] = [volume(box(1,1,1)), volume(box(2,2,2))]; print("D", x, y);' "$SRAVA" 2>/dev/null | grep "^D ")
	if [ "$O1" = "D 1 2" ] && [ "$O2" = "D 1 2" ] && [ "$O3" = "1" ] && [ "$O4" = "1" ] \
	   && [ "$O5" = "D 1 8" ]; then
		echo "DESTRUCT_OK"
	else
		echo "DESTRUCT_FAIL: [$O1] [$O2] need=$O3 arr=$O4 [$O5]"; fi ;;
async_err)
	# body のエラーは中断せず集積され末尾報告(continue-and-collect)。当該 async の sync はスキップ
	# されるが、他の async の sync は出る。exit code は 1。出力に "OTHER-ok" が在り "AFTER" が無く rc=1。
	OUT=$(SRAVA_SOURCE='async { print(box(1,1,1) + box(2,2,2)); sync: print("AFTER-skip"); }
	async { sync: print("OTHER-ok"); }' "$SRAVA" 2>/dev/null)
	RC=$?
	if printf '%s' "$OUT" | grep -q "OTHER-ok" && ! printf '%s' "$OUT" | grep -q "AFTER-skip" && [ "$RC" = "1" ]; then
		echo "ASYNCERR_OK"; else echo "ASYNCERR_FAIL: rc=$RC out=[$OUT]"; fi ;;
parseerr)
	# 構文エラーは「該当トークン + 行 + キャレット」で表示(セミコロン抜け → 次の export で検出)。
	EF="$D.sra"
	printf 'var a = box(1,1,1)\nexport(a);\n' > "$EF"
	"$SRAVA" "$EF" 2>&1 | grep -E "parse error near 'export'" | head -1 ;;
tofloat)
	# float(x): 文字列/整数を浮動小数へ変換(planner 側 op・agent 不要)。
	# float("1.5")+2=3.5、float(7)/2=3.5(=float 除算・int なら 7/2=3)、float("42")=42。出力 "F 3.5 3.5 42"。
	SRAVA_SOURCE='print("F", float("1.5")+2, float(7)/2, float("42"));' exec "$SRAVA" ;;
toint)
	# int(x): 文字列/浮動小数を整数へ変換(planner 側 op・agent 不要・浮動小数は 0 方向へ切り捨て)。
	# int("42")=42、int(3.9)=3(切り捨て)、int("7")+1=8。出力 "I 42 3 8"。
	SRAVA_SOURCE='print("I", int("42"), int(3.9), int("7")+1);' exec "$SRAVA" ;;
bbox)
	# bbox(mesh): 軸平行 AABB を [min隅, max隅] の入れ子配列で返す(2D/3D 多態)。添字 b[i][j] 可。
	# 3D box(2,3,4)>>>[1,1,1] → min[1,1,1] max[3,4,5]。2D rect(5,2)>>>[10,20] → min[10,20] max[15,22]。
	# 出力 "B 1 5 10 22"(bb[0][0], bb[1][2], c[0][0], c[1][1])。
	SRAVA_SOURCE='var bb = bbox(box(2,3,4) >>> [1,1,1]);
	var c = bbox(rect(5,2) >>> [10,20]);
	print("B", bb[0][0], bb[1][2], c[0][0], c[1][1]);' exec "$SRAVA" ;;
identity_export_err)
	# export({}) は実体化できないので明示エラー(クラッシュしない)。
	SRAVA_SOURCE='export("'$D'/x.stl", {});' "$SRAVA" 2>&1 \
	  | grep -E 'empty mesh \{\} cannot be exported' | head -1 ;;
polygon_dedup)
	# 曲線を concat した継ぎ目等で出る連続重複頂点を polygon が間引く → 単純多角形(valid=1)。
	# 重複が残ると非単純(valid=0)になり offset が空になる回帰。出力 "DEDUP 1"。
	SRAVA_SOURCE='print("DEDUP", valid(polygon([[0,0],[10,0],[10,0],[10,10],[0,10]])));' exec "$SRAVA" ;;
offset_err)
	# 不正(自己交差=蝶ネクタイ)ポリゴンの offset は黙って空 SVG でなく明示エラー(union 等と一貫)。
	SRAVA_SOURCE='export("'$D'/o.svg", offset(polygon([[0,0],[10,10],[10,0],[0,10]]), 1));' "$SRAVA" 2>&1 \
	  | grep -E 'offset failed' | head -1 ;;
add_lineno)
	# mesh + mesh(非対応 add)のエラー行は、被演算子の値が作られた行ではなく **+ の式の行**(=3行目)。
	SRAVA_SOURCE='var m = box(2,2,2);
	var n = box(2,2,2);
	var bad = m + n;
	print(bad);' "$SRAVA" 2>&1 | grep -E 'unsupported operation' | head -1 ;;
hash_sibling)
	# hash リテラル内で後のキー値が先のキーを参照できる(逐次スコープ・let* 相当)。
	# { b:1, c:b+2, d:c+b } → c=3, d=4。外側 b=100 は兄弟 b=1 が shadow。出力 "SIB 3 4"。
	SRAVA_SOURCE='var b = 100;
	var a = { b: 1, c: b + 2, d: c + b };
	print("SIB", a.c, a.d);' exec "$SRAVA" ;;
grid_axes)
	# grid 軸別 gap([gx,gy]=格子ピッチ) と 3D grid3(層は Z 自動)。
	# grid 列1の x = c*gx = 1*4 = 4(rect 原点は隅=0)。grid3 1x1 で 2 層 gz=10 → 層1 の z = L*gz = 1*10 = 10。
	# 出力 "GAXES 4 10"。
	SRAVA_SOURCE='include "std/layout.sra";
	var b = grid([rect(10,10), rect(30,10)], 2, [4, 20]);
	var c = grid3([box(10,10,10), box(10,10,10)], 1, 1, [3,3,10]);
	print("GAXES", bbox(b[1])[0][0], bbox(c[1])[0][2]);' exec "$SRAVA" ;;
dist_bool)
	# combine(+++)入力へのブール分配則。**重なる**2 箱を束ね(自己交差→分配経路)、slab で積/差。
	# (a+++b)&&&c = 500+500 = 1000 / (a+++b)---c = 500+500 = 1000。出力 "DIST 1000 1000"。
	SRAVA_SOURCE='var a = box(10,10,10); var b = box(10,10,10) >>> [5,0,0];
	var c = box(40,5,10);
	print("DIST", volume((a +++ b) &&& c), volume((a +++ b) --- c));' exec "$SRAVA" ;;
chain_assign)
	# 連鎖代入 a=b=c=expr(右結合・1度評価)＋括弧内埋め込み代入 (c=5)。
	# a=b=3 / x=y=x+y=3 / z=10+(c=5)=15・c=5。出力 "CA 6 3 3 15 5"(a+b, x, y, z, c)。
	SRAVA_SOURCE='var a; var b; var c; a = b = c = 1 + 2;
	var x = 1; var y = 2; x = y = x + y;
	var z = 10 + (c = 5);
	print("CA", a+b, x, y, z, c);' exec "$SRAVA" ;;
thin_spots)
	# 肉厚 SDF。薄板 10x10x0.6(12三角形)→ 閾値2.0・既定45°で12。厚塊 8x8x8 → 0。
	# cone=5°(ほぼ垂直)だと側面は厚さ方向を見ず10mm読む → 上下4面のみ。出力 "THIN 12 0 4"。
	SRAVA_SOURCE='var slab = box(10, 10, 0.6); var blk = box(8, 8, 8);
	print("THIN", length(thin_spots(slab, 2.0)), length(thin_spots(blk, 2.0)), length(thin_spots(slab, 2.0, 25, 5)));' exec "$SRAVA" ;;
export_formats)
	# 単位つき出力 AMF/3MF(どちらも自前・依存なし・全環境)。box∪sphere を両形式に書き、
	# AMF=unit 属性+三角形、3MF=ZIP(PK)先頭 かつ 中の 3dmodel.model に unit=inch を検証。
	AMF="$T/srava-exp.amf"; TMF="$T/srava-exp.3mf"
	rm -f "$AMF" "$TMF"
	SRAVA_SOURCE='var m = box(10,10,10) ||| sphere(6); export("'"$AMF"'", m, "mm"); export("'"$TMF"'", m, "inch");' "$SRAVA" >/dev/null 2>&1
	amf_ok=0
	if test -s "$AMF" && grep -q 'unit="millimeter"' "$AMF" && grep -q '<triangle>' "$AMF"; then amf_ok=1; fi
	tmf_ok=0
	if test -s "$TMF" && test "$(head -c2 "$TMF")" = "PK"; then
		if command -v unzip >/dev/null 2>&1; then
			unzip -p "$TMF" 3D/3dmodel.model 2>/dev/null | grep -q 'unit="inch"' && tmf_ok=1
		else
			# unzip 不在(Cygwin 既定)。srava の 3MF ZIP は STORE(無圧縮)なので
			# 3dmodel.model の XML が raw で見える → unzip なしで内容検証できる。
			grep -aq 'unit="inch"' "$TMF" && tmf_ok=1
		fi
	fi
	if [ "$amf_ok" = 1 ] && [ "$tmf_ok" = 1 ]; then echo "EXPFMT_OK"; else echo "EXPFMT_FAIL amf=$amf_ok tmf=$tmf_ok"; fi ;;
print_mesh_array)
	# 配列/ハッシュ内の mesh を print → 各要素が解決されキャッシュパス(.cache)が出る
	# (従来 (delayed . <delayed>) が漏れていた回帰)。delayed が出ず .cache が 2 つ出れば OK。
	OUT=$(SRAVA_SOURCE='print([box(2,2,2) ||| box(1,1,3), box(1,1,1)]);' "$SRAVA" 2>&1 | grep -v '^\[srava\]')
	if printf '%s' "$OUT" | grep -q 'delayed'; then echo "PMA_FAIL delayed: $OUT"
	elif [ "$(printf '%s' "$OUT" | grep -o '\.cache' | wc -l | tr -d '[:space:]')" = "2" ]; then echo "PMA_OK"
	else echo "PMA_FAIL: $OUT"; fi ;;
guide_ruler)
	# std/guide.sra の ruler(細い tube の ものさし)。box(10,10,10) +++ ruler(0,50,10,0.3) →
	# 主線が x=50 まで(端 cap 0.3)伸びるので bbox x-max ≈ 50.3。出力 "RULER 50.2..."。
	SRAVA_SOURCE='include "std/guide.sra";
	print("RULER", bbox(box(10,10,10) +++ ruler(0, 50, 10, 0.3))[1][0]);' exec "$SRAVA" ;;
color_export)
	# color(mesh,c) + combine の per-face 色。COFF に 赤(255 0 0)と青(0 0 255)の両方が出れば OK。
	OFF="$T/srava-color.off"
	rm -f "$OFF"
	SRAVA_SOURCE='var a = color(box(10,10,10), "red");
	var b = color(box(10,10,10) >>> [20,0,0], "blue");
	export("'"$OFF"'", a +++ b);' "$SRAVA" >/dev/null 2>&1
	if grep -q '255 0 0' "$OFF" && grep -q '0 0 255' "$OFF"; then echo "COLOR_OK"; else echo "COLOR_FAIL"; fi ;;
plugin_pipeprox)
	# pipe_proximity プラグイン: U 字に折り返すパイプ(両腕が接近)で自己接近を検出。
	# 太い半径(2.0)で reportGap 大きめにすると、折り返した腕どうしが接近 → 接近件数 > 0。
	OUT=$(SRAVA_SOURCE='
	var pts = [[0,0,0],[10,0.5,0],[10,3,0],[0,3.5,0]];
	var hits = pipe_proximity(pts, [1.0, 0.0], 4.0);
	print("PP", length(hits) > 0);' "$SRAVA" 2>&1 | grep '^PP')
	if [ "$OUT" = "PP 1" ]; then echo "PIPEPROX_OK"; else echo "PIPEPROX_FAIL: $OUT"; fi ;;
plugin_pipeprox_inproc)
	# ★ .so 化 Phase 5: in-proc 実行の証明。manifest の bin を **存在しないパス**にし、SRAVA_AGENT も
	#   偽にしても、pipe_proximity.so がロード済み (exec_default=THREAD) なら planner 内 thread
	#   (ppatsAgent) で完走する = process bin を一切 spawn していない証拠。値は process 版と一致 (PP 1)。
	OUT=$(SRAVA_AGENT=/nonexistent/BOGUS_AGENT SRAVA_SOURCE='
	module("pipe_proximity.so", {});
	var pts = [[0,0,0],[10,0.5,0],[10,3,0],[0,3.5,0]];
	var hits = pipe_proximity(pts, [1.0, 0.0], 4.0);
	print("PP", length(hits) > 0);' "$SRAVA" 2>&1 | grep '^PP')
	if [ "$OUT" = "PP 1" ]; then echo "PIPEINPROC_OK"; else echo "PIPEINPROC_FAIL: $OUT"; fi ;;
plugin_pipeprox_process)
	# ★ Plan A (2026-08-10): process 版の存続。module(so,{exec_default:"process"}) で in-proc を opt-out
	#   すると、汎用 host **srava_agent** が pipe_proximity.so を dlopen して ppatsAgent を別プロセス実行する
	#   (旧 pipe_proximity_agent 専用バイナリ + .plugin manifest は廃止)。値は in-proc と一致 (PP 1)。
	OUT=$(SRAVA_SOURCE='
	module("pipe_proximity.so", {exec_default:"process"});
	var pts = [[0,0,0],[10,0.5,0],[10,3,0],[0,3.5,0]];
	var hits = pipe_proximity(pts, [1.0, 0.0], 4.0);
	print("PP", length(hits) > 0);' "$SRAVA" 2>&1 | grep '^PP')
	if [ "$OUT" = "PP 1" ]; then echo "PIPEPROC_OK"; else echo "PIPEPROC_FAIL: $OUT"; fi ;;
module_demo_ops)
	# ★ 第3モジュール実証 (Phase 6・完成条件): demo.so を探索路 (srava と同 dir) に置くだけで新 op が
	#   使える (host 無改修)。module("demo.so",{priority:99}) で demo を既定カーネル化 → generic mk_call 層が
	#   demo_add/demo_range を pigfKernelAgent ノードとして受理 → EXEC_PROCESS で srava_agent+demo.so 実行。
	OUT=$(SRAVA_SOURCE='
	module("demo.so", {priority:99});
	var ok = 0;
	if (demo_add(2, 3) == 5) { if (length(demo_range(4)) == 4) { if (demo_range(4)[3] == 3) { ok = 1; } } }
	print("DEMO", ok);' "$SRAVA" 2>&1 | grep "^DEMO")
	if [ "$OUT" = "DEMO 1" ]; then echo "DEMO_OK"; else echo "DEMO_FAIL: $OUT"; fi ;;
module_d3_mesh)
	# ★ 第3(mesh 出力)カーネル実証 (rev4 Phase D-3): d3.so を探索路に置くだけで mesh op が使え、
	#   codec/wire-stream/cache 往復が成立。d3_cube(s)→mesh・d3_merge(a,b)→連結 mesh(16v/24f)・
	#   d3_nfaces/d3_nverts→値。module("d3.so",{priority:99}) で d3 を既定カーネル化 (mesh leaf も d3 へ)。
	OUT=$(SRAVA_SOURCE='
	module("d3.so", {priority:99});
	var m = d3_merge(d3_cube(1), d3_cube(2));
	var ok = 0;
	if (d3_nfaces(m) == 24) { if (d3_nverts(m) == 16) { ok = 1; } }
	print("D3", ok);' "$SRAVA" 2>&1 | grep "^D3")
	if [ "$OUT" = "D3 1" ]; then echo "D3_OK"; else echo "D3_FAIL: $OUT"; fi ;;
route_no_sig)
	# ★ #3440: 型で解決できない呼び出しは **明示エラー**であること。
	#   d3_nfaces の sig は "(d3-mesh3d)->value" だけなので、cgal の箱 (cg-mesh3d) を渡すと
	#   受ける sig が無い = routing 不能。
	#   撤去前は「その op 名を実装するモジュールが 1 つだけならそこへ直送」という経路があり、
	#   d3 まで届いてから読めずに落ちていた (原因が sig の書き漏らしだと分からないエラーになる)。
	#   型でなく **op 名**で振る概念は srava の設計に無い (ひさ指摘) ので撤去した。
	OUT=$(SRAVA_SOURCE='
	module("d3.so");
	print("N", d3_nfaces(box(2,2,2)));' "$SRAVA" 2>&1)
	if echo "$OUT" | grep -q "^N " ; then
		echo "ROUTE_FAIL: 値が返った (routing 不能でエラーになるべき): $OUT" ; exit 0
	fi
	if ! echo "$OUT" | grep -q "no module can execute op" ; then
		echo "ROUTE_FAIL: 明示エラーになっていない: $OUT" ; exit 0
	fi
	# 原因が分かるメッセージであること (op 名と入力型が出る)
	if ! echo "$OUT" | grep -q "d3_nfaces" || ! echo "$OUT" | grep -q "cg-mesh3d" ; then
		echo "ROUTE_FAIL: メッセージに op 名か入力型が無い: $OUT" ; exit 0
	fi
	echo "ROUTE_OK" ;;
module_d4_mesh)
	# ★ 第4モジュール d4 のネイティブ mesh 往復 (⑤ P4 の健全性確認): d3 と同じ立方体 2 個 (16v/24f)。
	#   d4 は exec_default=THREAD なので同型 (d4→d4) は in-proc fast path (in-memory 共有・codec 非経由)。
	OUT=$(SRAVA_SOURCE='
	module("d4.so", {priority:99});
	var m = d4_merge(d4_cube(1), d4_cube(2));
	var ok = 0;
	if (d4_nfaces(m) == 24) { if (d4_nverts(m) == 16) { ok = 1; } }
	print("D4", ok);' "$SRAVA" 2>&1 | grep "^D4")
	if [ "$OUT" = "D4 1" ]; then echo "D4_OK"; else echo "D4_FAIL: $OUT"; fi ;;
plugin_pipeadjust)
	# pipe_adjust プラグイン op(同一 bin が pipe_proximity と両 serve): クリアランス違反の
	# 折り返しを端点固定で開き、gap >= dMin を満たす(feasible=1 かつ clearViolation≈0)。
	OUT=$(SRAVA_SOURCE='
	module("pipe_proximity.so", {});
	var pts = [[0,0,0],[12,0,0],[10,2,0],[12,4,0],[0,4,0]];
	var res = pipe_adjust(pts, [0.8, 0.0], 0.6, 400, 1, 0.1);
	var ok = 0;
	if (res.feasible == 1) { if (res.clearViolation < 0.05) { ok = 1; } }
	print("PA", ok);' "$SRAVA" 2>&1 | grep '^PA')
	if [ "$OUT" = "PA 1" ]; then echo "PIPEADJUST_OK"; else echo "PIPEADJUST_FAIL: $OUT"; fi ;;
plugin_radius)
	# 半径プロファイル: スカラ一定 0.8 と、[s,r] キーポイント一定 0.8(クランプで全域 0.8)が
	# 同じ gap を返すこと(線形補間経路の健全性)。
	OUT=$(SRAVA_SOURCE='
	module("pipe_proximity.so", {});
	var pts = [[0,0,0],[12,0,0],[10,2,0],[12,4,0],[0,4,0]];
	var a = pipe_proximity(pts, 0.8, 8.0)[0][0];
	var b = pipe_proximity(pts, [[0,0.8],[1000,0.8]], 8.0)[0][0];
	var d = a - b; if (d < 0) { d = 0 - d; }
	print("RAD", d < 0.0001);' "$SRAVA" 2>&1 | grep '^RAD')
	if [ "$OUT" = "RAD 1" ]; then echo "PIPERADIUS_OK"; else echo "PIPERADIUS_FAIL: $OUT"; fi ;;
plugin_scene)
	# N 体(Scene): 可動配管 body0 + 固定障害物 body1。近接検出(>0)し、adjustScene で
	# body0 を gap>=dMin へ調整(feasible=1 かつ clearViolation≈0)。同一 bin の scene 系 2 op。
	OUT=$(SRAVA_SOURCE='
	module("pipe_proximity.so", {});
	var bodies = [
	  {ctrl: [[0,2,0],[14,2,0],[0,2.2,0]], radius: 0.8, movable: 1},
	  {ctrl: [[0,4,0],[14,4,0]],           radius: 0.8, movable: 0}
	];
	var nb = pipe_scene_proximity(bodies, 8.0);
	// solver:"cd"(座標降下)。改良 circleCircle(swept-disk)の正直な深さでは既定の勾配降下は
	// この深い重なりを解ききれない。cd は確実に gap>=dMin へ収束する(clearViolation~0)。
	var rs = pipe_scene_adjust(bodies, 0, {dMin: 0.5, maxIter: 400, fixEnds: 1, solver: "cd"});
	var ok = 0;
	if (length(nb) > 0) { if (rs.feasible == 1) { if (rs.clearViolation < 0.05) { ok = 1; } } }
	print("SC", ok);' "$SRAVA" 2>&1 | grep "^SC")
	if [ "$OUT" = "SC 1" ]; then echo "PIPESCENE_OK"; else echo "PIPESCENE_FAIL: $OUT"; fi ;;
plugin_sample)
	# pipe_sample: 弧長等間隔サンプル。テーパ半径で先頭 r≈1.2(キーポイント厳密)・末尾<先頭、
	# 隣接間隔が指定ピッチ 1.5 にほぼ一致(弧長等間隔)。
	OUT=$(SRAVA_SOURCE='
	module("pipe_proximity.so", {});
	var pts = [[0,0,0],[12,0,0],[10,2,0],[12,4,0],[0,4,0]];
	var d = pipe_sample(pts, [[0,1.2],[100,0.4]], 1.5);
	var rf = d[0][1]; var rl = d[length(d)-1][1];
	var g = d[2][0] - d[1][0]; var p = sqrt(sum(g*g));
	var ok = 0;
	if (rf > 1.19) { if (rf < 1.21) { if (rl < rf) { if (p > 1.4) { if (p < 1.6) { ok = 1; } } } } }
	print("SM", ok);' "$SRAVA" 2>&1 | grep "^SM")
	if [ "$OUT" = "SM 1" ]; then echo "PIPESAMPLE_OK"; else echo "PIPESAMPLE_FAIL: $OUT"; fi ;;
plugin_separate)
	# 射影的分離パス: ピッチ≈2r で隣接ターンが接触する 2 周コイルを、pipe_adjust が
	# gap>=dMin へ押し広げる(energy 法では取れない重なり解消)。before≈0 → after≈dMin。
	OUT=$(SRAVA_SOURCE='
	include "std/math.sra";
	module("pipe_proximity.so", {});
	var ctr=6; var R=12; var r=4; var N=2;
	var sp=linspace(0,2*PI*N,N*ctr); var x=cos(sp)*R; var y=sin(sp)*R; var z=linspace(0,-2*r*N,N*ctr);
	var coil=transpose([x,y,z]);
	var bh=pipe_proximity(coil,[r,0],4*r); var before=999.0; if(length(bh)>0){before=bh[0][0];}
	var res=pipe_adjust(coil,[r,0],{dMin:0.5,fixEnds:0,maxIter:200});
	var ah=pipe_proximity(res.ctrl,[r,0],4*r); var after=999.0; if(length(ah)>0){after=ah[0][0];}
	var ok=0; if(before<0.2){ if(after>0.4){ ok=1; } } print("SEP",ok);' "$SRAVA" 2>&1 | grep "^SEP")
	if [ "$OUT" = "SEP 1" ]; then echo "PIPESEPARATE_OK"; else echo "PIPESEPARATE_FAIL: $OUT"; fi ;;
pipeprox_fixed_force)
	# ★ #3408 回帰: fixed 指定の制御点が外力(fZ)下で本当に固定されるか。
	#   真因は energy solver ではなく後段 polishScene: 接触フリー区間の貪欲拡張が
	#   blocked(端点・固定 DOF・接触 DOF・硬ピン)を跨いで relaxSpan し、跨いだ点を可動化していた。
	#   外力下は点を下げるほど energy が下がるので keep-if-lower ガードも効かず固定点が落下する
	#   (バグ時の実測 z=[0,-1600,-1592,-432,-8,...] → 修正後 z=[0,0,0,0,0,...])。
	#   ★ 接触が在ることが再現条件: polish は「接触フリー区間」を起点に境界を広げるので、
	#     自己接触のあるコイル(ピッチ≈2r)でなければこの経路に入らない(直線では踏めない)。
	OUT=$(SRAVA_SOURCE='
	include "std/math.sra";
	module("pipe_proximity.so", {});
	var ctr=6; var R=12; var r=4; var N=2;
	var sp=linspace(0,2*PI*N,N*ctr); var x=cos(sp)*R; var y=sin(sp)*R; var z=linspace(0,-2*r*N,N*ctr);
	var coil=transpose([x,y,z]);
	var res=pipe_adjust(coil,[r,0],
	    {dMin:0.5, fixEnds:1, maxIter:200, solver:"cd", fixed:[0,1,2,3,4], fZ:-0.1});
	var worst = 0;
	var i;
	for ( i = 0 ; i < 5 ; i = i + 1 ) {
		var d = res.ctrl[i][2] - coil[i][2];
		if ( d < 0 ) { d = 0 - d; }
		if ( d > worst ) { worst = d; }
	}
	print("PF", worst < 0.001);' "$SRAVA" 2>&1 | grep "^PF")
	if [ "$OUT" = "PF 1" ]; then echo "PIPEFIXED_OK"; else echo "PIPEFIXED_FAIL: $OUT"; fi ;;
pipeprox_wspace)
	# wSpace(制御点間隔の均一化・上流 pipeProximity v0.1.7 の項)。長さ/曲げ項は制御点を曲線に沿って
	#   スライドさせる変形にほぼ不感(ヌルモード)なので、不均一な間隔は既定 wSpace=0 では是正されない。
	#   wSpace>0 が「間隔を揃えるばね」として効くことを、区間長²の最大最小比で判定する
	#   (実測: 入力 92 → wSpace:0 で 92.2 のまま / wSpace:0.1 で 1.016 まで均一化)。
	OUT=$(SRAVA_SOURCE='
	module("pipe_proximity.so", {});
	var pts = [[0,0,0],[5,0,0],[12,0,0],[60,0,0],[95,0,0],[100,0,0]];
	var ratio = \(c) {
		var n = length(c); var i; var mn = 1e30; var mx = 0;
		for ( i = 1 ; i < n ; i = i + 1 ) {
			var dx = c[i][0]-c[i-1][0]; var dy = c[i][1]-c[i-1][1]; var dz = c[i][2]-c[i-1][2];
			var s = dx*dx + dy*dy + dz*dz;
			if ( s < mn ) { mn = s; }
			if ( s > mx ) { mx = s; }
		}
		return mx / mn;
	};
	var a = pipe_adjust(pts, [2.0, 0.0], {dMin: 1.0, maxIter: 40, fixEnds: 1, solver: "cd"});
	var b = pipe_adjust(pts, [2.0, 0.0], {dMin: 1.0, maxIter: 40, fixEnds: 1, solver: "cd", wSpace: 0.1});
	var ra = ratio(a.ctrl); var rb = ratio(b.ctrl);
	var ok = 0;
	if ( ra > 50 ) { if ( rb < 1.1 ) { if ( b.feasible == 1 ) { ok = 1; } } }
	print("WS", ok);' "$SRAVA" 2>&1 | grep "^WS")
	if [ "$OUT" = "WS 1" ]; then echo "PIPEWSPACE_OK"; else echo "PIPEWSPACE_FAIL: $OUT"; fi ;;
*)
	echo "unknown mode: $MODE"; exit 2 ;;
esac
