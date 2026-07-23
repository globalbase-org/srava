#!/bin/sh
# lemonc++ パーサの回帰テスト。$1=srava 実行体, $2=モード。SRAVA_SOURCE をここで設定する
# (セミコロンを含むので cmake の ENVIRONMENT 経由ではなくスクリプト内で渡す)。
SRAVA="$1"
MODE="$2"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# Windows(MSYS): native srava は "/tmp" を C:\tmp、MSYS sh は C:\msys64\tmp と解決するため
# 出力先とチェック先が食い違う。cygpath で両者一致の native 形へ: D(=cache dir。$D.ext を出力に使う
# ケースを一括で救う)と、直書き /tmp の代替 T。Linux は cygpath 不在 → 従来どおり(/tmp のまま)。
T=/tmp
if command -v cygpath >/dev/null 2>&1; then T=$(cygpath -m /tmp); D=$(cygpath -m "$D"); fi
rm -rf "$D"
case "$MODE" in
callform)
	SRAVA_SOURCE='export(union(box(2,2,2), box(1,1,3)));' exec "$SRAVA" ;;
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
	printf '#!/usr/bin/env srava\n// shebang + file 実行テスト\nexport(box(2,2,2) ||| box(1,1,3));\n' > "$F"
	exec "$SRAVA" "$F" ;;
intersection)
	SRAVA_SOURCE='export(box(2,2,2) &&& box(1,1,3));' exec "$SRAVA" ;;
difference)
	SRAVA_SOURCE='export(box(2,2,2) --- box(1,1,3));' exec "$SRAVA" ;;
ifaccum)
	# if + ブロック + 比較 + 自己代入(strict SET)。取られる枝で a を union に更新 → 25v46f
	SRAVA_SOURCE='var a = box(2,2,2); if (1==1) { a = a ||| box(1,1,3); } export(a);' exec "$SRAVA" ;;
ifskip)
	# 条件偽 → ブロック実行されず a は box のまま → 8v12f
	SRAVA_SOURCE='var a = box(2,2,2); if (1==2) { a = a ||| box(1,1,3); } export(a);' exec "$SRAVA" ;;
prism)
	SRAVA_SOURCE='export(prism(6,2,1));' exec "$SRAVA" ;;
pyramid)
	SRAVA_SOURCE='export(pyramid(4,2,1));' exec "$SRAVA" ;;
sphere)
	SRAVA_SOURCE='export(sphere(1));' exec "$SRAVA" ;;
arrayidx)
	# array リテラル + 添字参照: a[0] ||| a[1] = union(box,box) = 25v46f
	SRAVA_SOURCE='var a = [box(2,2,2), box(1,1,3)]; export(a[0] ||| a[1]);' exec "$SRAVA" ;;
hashlit)
	# source 側 hash リテラル {k:v,..} + メンバ参照: h.a ||| h.b = union(box,box) = 25v46f
	SRAVA_SOURCE='var h = {a: box(2,2,2), b: box(1,1,3)}; export(h.a ||| h.b);' exec "$SRAVA" ;;
inlineval)
	# 構造 inline 引数: array を serialize→wire→agent で value-parse→cgaBox 展開。
	# hash メンバ→array も経由。boxa([1,1,3]) = 直方体 = 8v12f。
	SRAVA_SOURCE='var h = {dims: [1,1,3]}; export(boxa(h.dims));' exec "$SRAVA" ;;
lambda)
	# lambda + apply(clone/thunk): u(box(2,2,2)) = box(2,2,2) ||| box(1,1,3) = 25v46f。
	SRAVA_SOURCE='var u = \(s){ s ||| box(1,1,3); }; export(u(box(2,2,2)));' exec "$SRAVA" ;;
lambda_reapply)
	# 同一 lambda を別引数で再 apply(body->clone() でメモ衝突回避)。2 union の union = 33v62f。
	SRAVA_SOURCE='var u = \(s){ s ||| box(1,1,3); }; export(u(box(2,2,2)) ||| u(box(3,3,3)));' exec "$SRAVA" ;;
lambda_closure)
	# クロージャ(カリー化): adder(a) が a を捕捉した lambda を返す。add1(box(1,1,3)) = 25v46f。
	SRAVA_SOURCE='var adder = \(a){ \(b){ a ||| b; }; }; var add1 = adder(box(2,2,2)); export(add1(box(1,1,3)));' exec "$SRAVA" ;;
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
	# PIG_MAX_WORKERS=2 は ENVIRONMENT で注入。union(6 箱)の二分木は深さ>2。完走すれば末尾サマリが出る。
	SRAVA_SOURCE='export(union([box(1,1,1), box(1,1,1)>>>[2,0,0], box(1,1,1)>>>[4,0,0], box(1,1,1)>>>[0,2,0], box(1,1,1)>>>[2,2,0], box(1,1,1)>>>[4,2,0]]));' exec "$SRAVA" ;;
workergate_eagain)
	# PIG_TEST_FORKLIMIT=2(同時 fork>2 を失敗させる)< PIG_MAX_WORKERS=32(高め・env)。
	# limit 固定方針なので backoff せず "Lower PIG_MAX_WORKERS and re-run" の明確なエラーで終了する
	# (黙ったデッドロック/ハングを避け、ユーザに cap を下げて再実行してもらう)。
	SRAVA_SOURCE='export(union([box(1,1,1), box(1,1,1)>>>[2,0,0], box(1,1,1)>>>[4,0,0], box(1,1,1)>>>[0,2,0], box(1,1,1)>>>[2,2,0], box(1,1,1)>>>[4,2,0]]));' exec "$SRAVA" ;;
fdleak)
	# fd リーク回帰: ulimit -n を 64 に絞って ~160 agent を回す。FIN で rfd/pipe を閉じないと
	# fd が枯渇し pipe()/fork が EMFILE で "failed to launch agent"(macOS 256 で顕在化した真因)。
	# 修正後は fd が再利用され完走して "result cache" が出る。
	ulimit -n 64 2>/dev/null
	SRAVA_SOURCE='var p=[]; var i; for(i=0;i<80;i=i+1){ p=concat(p, prism(3+i,2,1)>>>[i*1.0,0,0]); } export("/tmp/srava-fdleak.stl", combine(p));' exec "$SRAVA" ;;
while_loop)
	# while(毎周 clone 再評価): i=1,2 で box(i,i,9) を union 蓄積。box(5,5,5)|||box(1,1,9)|||box(2,2,9)
	# = 52v100f。i が進む(共有 env への代入)+ 毎周別形状(clone)を検証。
	SRAVA_SOURCE='var i = 1; var acc = box(5,5,5); while (i < 3) { acc = acc ||| box(i,i,9); i = i + 1; } export(acc);' exec "$SRAVA" ;;
for_loop)
	# for(init;cond;step) → while desugar。while_loop と等価 = 52v100f。
	SRAVA_SOURCE='var acc = box(5,5,5); for (var i = 1; i < 3; i = i + 1) { acc = acc ||| box(i,i,9); } export(acc);' exec "$SRAVA" ;;
for_nested)
	# 入れ子 for(i,j 各 1..2)。内側 for は外側 body の clone で毎周新鮮 j に再初期化される。
	# box(9,9,9)|||box(i,j,7) 4 個 = 33v62f。
	SRAVA_SOURCE='var acc = box(9,9,9); for (var i = 1; i < 3; i = i+1) { for (var j = 1; j < 3; j = j+1) { acc = acc ||| box(i,j,7); } } export(acc);' exec "$SRAVA" ;;
apply_chain)
	# 一般呼び出し: 中間変数なしの直接/連鎖適用。adder(box)(box) = カリー化を直に適用 = 25v46f。
	SRAVA_SOURCE='var adder = \(a){ \(b){ a ||| b; }; }; export(adder(box(2,2,2))(box(1,1,3)));' exec "$SRAVA" ;;
recursion)
	# 再帰 lambda(引数 call-by-value で変数捕捉を回避)。box(i,i,9) を i=2,1 と再帰 union、
	# 基底 box(5,5,5)。= 52v100f(while/for 版と同形状)。算術 n-1 を引数に渡す自己再帰を検証。
	SRAVA_SOURCE='var f = \(n){ if (n < 1) { box(5,5,5); } else { box(n,n,9) ||| f(n - 1); } }; export(f(2));' exec "$SRAVA" ;;
import)
	# import(path): box(2,2,2) を STL に書き → import で読み戻し(soup repair)→ box(1,1,3) と union
	# = 25v46f。export→import 往復 + 拡張子判別 + DAG 葉としての利用を検証。
	SRAVA_SOURCE='export("/tmp/srava-import-test.stl", box(2,2,2)); export(import("/tmp/srava-import-test.stl") ||| box(1,1,3));' exec "$SRAVA" ;;
nary)
	# 実行木分解: n-ary 可換呼び出し union(a,b,c) をプランナーが二項木に分解(agent は二項のみ)。
	# 3 box の union = 27v50f。
	SRAVA_SOURCE='export(union(box(2,2,2), box(1,1,3), box(5,5,5)));' exec "$SRAVA" ;;
import_err)
	# import 失敗(不在ファイル)は サイレント空メッシュでなく明示エラーになること(nv= を出さない)。
	rm -f /tmp/srava-import-missing.stl
	SRAVA_SOURCE='export(import("/tmp/srava-import-missing.stl"));' exec "$SRAVA" ;;
import_err_union)
	# 失敗 import が下流 agent(union)の上流にある場合: クリーンな import エラーが伝播し(arg type
	# /index mismatch でなく)、起動済み orphan agent でハングしないこと(TIMEOUT で検出)。
	rm -f /tmp/srava-import-missing2.stl
	SRAVA_SOURCE='export(import("/tmp/srava-import-missing2.stl") ||| box(1,1,3));' exec "$SRAVA" ;;
xlate)
	# translate(m,x,y,z): box を +1 移動して原位置 box と union(重なり)= 24v44f。座標が動く証明。
	SRAVA_SOURCE='export(box(2,2,2) ||| translate(box(2,2,2), 1, 0, 0));' exec "$SRAVA" ;;
rotate)
	# rotate(m,axis,deg): 45° 回転(任意角→double cos/sin)。原位置 box と union で星型重なり = 22v40f。
	SRAVA_SOURCE='export(box(4,4,1) ||| rotate(box(4,4,1), "z", 45));' exec "$SRAVA" ;;
mirror)
	# mirror(m,axis): x=3 に寄せた box を x 鏡像 → x=-3。union で分離 2 個 = 16v24f。向き補正で union 成立。
	SRAVA_SOURCE='export(translate(box(1,2,2), 3,0,0) ||| mirror(translate(box(1,2,2), 3,0,0), "x"));' exec "$SRAVA" ;;
xform)
	# transform(m,matrix): 3x4 行列の平行移動列で +1 移動 = translate と等価。union 重なり = 24v44f。
	SRAVA_SOURCE='export(box(2,2,2) ||| transform(box(2,2,2), [1,0,0,1, 0,1,0,0, 0,0,1,0]));' exec "$SRAVA" ;;
rotate_err)
	# 未対応 axis は明示エラー(サイレント無視でない)。
	SRAVA_SOURCE='export(rotate(box(1,1,1), "w", 30));' exec "$SRAVA" ;;
op_xlate)
	# 演算子 >>> = translate。||| より強く結合(括弧なし)→ box ||| (box>>>[1,0,0]) = 24v44f。
	SRAVA_SOURCE='export(box(2,2,2) ||| box(2,2,2) >>> [1,0,0]);' exec "$SRAVA" ;;
op_rotate)
	# 演算子 @(axis,d) = rotate。box(4,4,1) ||| (box @ ("z",45)) = 22v40f。
	SRAVA_SOURCE='export(box(4,4,1) ||| box(4,4,1) @ ("z", 45));' exec "$SRAVA" ;;
op_mirror)
	# 演算子 <> = mirror。x=3 の箱を <>"x" で x=-3 へ、union 分離 = 16v24f。
	SRAVA_SOURCE='export((box(1,2,2) >>> [3,0,0]) ||| (box(1,2,2) >>> [3,0,0]) <> "x");' exec "$SRAVA" ;;
vec_axis)
	# ベクトル軸回転 rotate(m,[0,0,1],deg) は文字列 "z" と同結果(任意軸 Rodrigues の主軸特例)= 22v40f。
	SRAVA_SOURCE='export(box(4,4,1) ||| rotate(box(4,4,1), [0,0,1], 45));' exec "$SRAVA" ;;
vec_degenerate)
	# 退化軸ベクトル [0,0,0] は明示エラー(正規化不能)。
	SRAVA_SOURCE='export(rotate(box(1,1,1), [0,0,0], 30));' exec "$SRAVA" ;;
scale_uniform)
	# 均等スケール: box(1,1,1)*2 = box(2,2,2) と完全重複 → union 8v12f。
	SRAVA_SOURCE='export(box(2,2,2) ||| scale(box(1,1,1), 2));' exec "$SRAVA" ;;
scale_op)
	# 演算子 *** 均等。box(1,1,1)***2 = box(2,2,2) と重複 8v12f。
	SRAVA_SOURCE='export(box(2,2,2) ||| box(1,1,1) *** 2);' exec "$SRAVA" ;;
scale_vec)
	# 軸別スケール(配列)= 3スカラと同一(計算本体で判別)。離れた位置で union 確認。
	SRAVA_SOURCE='export(box(1,1,1) ||| (scale(box(1,1,1), [2,3,4]) >>> [5,0,0]));' exec "$SRAVA" ;;
scale_err)
	# 退化(0)スケールは明示エラー(メッシュが潰れる)。
	SRAVA_SOURCE='export(scale(box(1,1,1), 0));' exec "$SRAVA" ;;
neg_literal)
	# 単項マイナス: 負方向移動 box>>>[-3,0,0] は元 box と分離 → 16v24f。配列内負値の round-trip 検証。
	SRAVA_SOURCE='export(box(1,1,1) >>> [-3,0,0] ||| box(1,1,1));' exec "$SRAVA" ;;
extrude)
	# 2D→3D: rect(2,1) を高さ 3 で extrude = 直方体 8v12f。cgMesh2D(PLY2)→ reader 多態 → cgMesh3D。
	SRAVA_SOURCE='export(extrude(rect(2,1), 3));' exec "$SRAVA" ;;
poly2d_union)
	# 2D ブーリアン(Polygon_set_2)+ 2D translate(apply_affine)。重なる 2 正方形の和 = L 字、extrude。
	SRAVA_SOURCE='export(extrude(rect(2,2) ||| (rect(2,2) >>> [1,1,0]), 1));' exec "$SRAVA" ;;
extrude_union3d)
	# 2D→3D extrude した角柱が 3D ブール(corefinement)に乗る(多態スピンの end-to-end)。
	SRAVA_SOURCE='export(box(5,5,5) ||| extrude(rect(2,1), 3));' exec "$SRAVA" ;;
mixed_dim_err)
	# 2D ||| 3D は型ガードで明示エラー(op_union が null → A_ERROR)。
	SRAVA_SOURCE='export(rect(2,2) ||| box(1,1,1));' exec "$SRAVA" ;;
prim_ngon)
	# 正六角形 extrude = 六角柱 12v20f。任意角頂点(cos/sin)。
	SRAVA_SOURCE='export(extrude(ngon(6, 1), 2));' exec "$SRAVA" ;;
prim_circle)
	# 円(32 角形近似)extrude = 円柱 64v124f。
	SRAVA_SOURCE='export(extrude(circle(1), 2));' exec "$SRAVA" ;;
prim_circle_segs)
	# circle 精度ピッチ(第2引数=辺数): circle(1,8)=八角形 → extrude 八角柱 16v28f。
	SRAVA_SOURCE='export(extrude(circle(1, 8), 2));' exec "$SRAVA" ;;
prim_sphere_subdiv)
	# sphere 精度ピッチ(第2引数=icosphere 細分化): sphere(1,2)=320 面の滑らか球 162v320f。
	# 既定 sphere(1) は subdiv 0(20 面・従来同形状)で別テスト sphere が担保。
	SRAVA_SOURCE='export(sphere(1, 2));' exec "$SRAVA" ;;
prim_polygon)
	# 明示点列(時計回りでも CCW 正規化)→ 三角柱 6v8f。
	SRAVA_SOURCE='export(extrude(polygon([[0,0],[1,2],[2,0]]), 1));' exec "$SRAVA" ;;
prim_polygon_err)
	# 2 点は多角形でない → 明示エラー。
	SRAVA_SOURCE='export(extrude(polygon([[0,0],[1,1]]), 1));' exec "$SRAVA" ;;
extrude_hole)
	# 穴対応 extrude(CDT): 4x4 から中央 2x2 を引いた額縁を立体化 → トンネル付きプリズム 16v32f。
	SRAVA_SOURCE='export(extrude(rect(4,4) --- (rect(2,2) >>> [1,1,0]), 1));' exec "$SRAVA" ;;
extrude_hole_union)
	# 額縁プリズムが閉多様体・向き正しい証明: box との corefinement union が通る → 19v34f。
	SRAVA_SOURCE='export(box(10,10,10) ||| extrude(rect(4,4) --- (rect(2,2) >>> [1,1,0]), 1));' exec "$SRAVA" ;;
rotate2d)
	# 2D rotate 軸不要(単一角度=z 面内回転)。演算子 @(deg)。位相は 8v12f。
	SRAVA_SOURCE='export(extrude(rect(2,1) @ (45), 1));' exec "$SRAVA" ;;
area_expr)
	# 値返し op(area)を式で観測(cold cache/MISS 経路)。2D 面積 rect(2,3)=6 を == で判定 →
	# 真なら union(25v46f)。値の VALUE 復元 + 演算子の継続 deref + A_SAVE_BEGIN 本文を検証。
	SRAVA_SOURCE='if (area(rect(2,3)) == 6) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
area_arith)
	# 値の算術 + 2 つの値 op 比較(volume(v1)==volume(v2) パターン)。area(2,3)+area(1,1)=7 → 25v46f。
	SRAVA_SOURCE='var s = area(rect(2,3)) + area(rect(1,1)); if (s == 7) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
cachedir_ctl)
	# task2: キャッシュ dir 初期化を first-agent 頭へ移動 + mkdir -p。
	# プログラムが CACHE_DIR を(env 既定を上書きして)深い新規 dir に設定 → mkdir -p で作成し
	# そこに cache が落ちる。export 成功(25v46f)= mkdir -p と first-agent 初期化が効いている証拠。
	D="/tmp/srava-cachectl-deep/x/y/z"
	rm -rf /tmp/srava-cachectl-deep
	SRAVA_SOURCE="CACHE_DIR = \"$D\"; export(box(2,2,2) ||| box(1,1,3));" exec "$SRAVA" ;;
lexical_shadow)
	# レキシカルスコープ(eager-DEF): var b は外側 sz.w=2 で定義 → 内側ブロックで sz=0 が
	# シャドウしても b は外側 sz を参照(dynamic scope なら 0.w でエラー)。box(2,2,2)|||box(1,1,3)=25v46f。
	SRAVA_SOURCE='var sz={w:2}; var b=box(sz.w,sz.w,sz.w); { var sz=0; export(b ||| box(1,1,3)); }' exec "$SRAVA" ;;
arr_varref)
	# 配列構築 `[..]` を演算子化(pigDataOperatorArray)した回帰: インライン配列内の varref が
	# **ネストした agent op(union の mesh 引数)**でも正しい env で解決される。
	# box(1,1,1) を [d,0,0] で平行移動 → box(2,2,2) と非接触 union = 2 箱 = 16v24f。
	SRAVA_SOURCE='var d=4; export(box(2,2,2) ||| (box(1,1,1) >>> [d,0,0]));' exec "$SRAVA" ;;
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
	SRAVA_SOURCE='if (volume(box(2,2,2)) == volume(box(1,1,3))) { export(box(1,1,1)); } else { export(box(2,2,2) ||| box(1,1,3)); }' exec "$SRAVA" ;;
valid_ok)
	# 検査(値返し): 健全な 3D box は valid==1 → 真なら union(25v46f)。値 VALUE 復元を検証。
	SRAVA_SOURCE='if (valid(box(2,2,2)) == 1) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
valid_bad)
	# 自己交差 2D(bowtie)を polygon() で作れる(検査緩和)→ valid==0 を検出 → 真なら 25v46f。
	SRAVA_SOURCE='if (valid(polygon([[0,0],[2,2],[2,0],[0,2]])) == 0) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
repair_2d)
	# 2D 修復: bowtie を repair(even-odd)→ 2 三角形に正規化 → valid==1。repair(mesh 返し)+valid(値)合成。
	SRAVA_SOURCE='if (valid(repair(polygon([[0,0],[2,2],[2,0],[0,2]]))) == 1) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
repair_3d)
	# 3D 修復: 健全 box は autorefine 無変化 → cache を経て union が通る(repair が usable mesh を返す証明)。25v46f。
	SRAVA_SOURCE='export(box(2,2,2) ||| repair(box(1,1,3)));' exec "$SRAVA" ;;
volume)
	# 計測(値返し): 3D box(2,2,2) の体積=8 → 真なら 25v46f。発散定理ベース。
	SRAVA_SOURCE='if (volume(box(2,2,2)) == 8) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
volume_err)
	# 2D に体積はない → エラー(area を使えと案内)。
	SRAVA_SOURCE='export(volume(rect(2,3)));' exec "$SRAVA" ;;
perimeter)
	# 計測(値返し): 2D rect(2,3) の境界長=2*(2+3)=10 → 真なら 25v46f。
	SRAVA_SOURCE='if (perimeter(rect(2,3)) == 10) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
centroid_2d)
	# 計測(配列返し): rect(2,3) の面積重心=[1,1.5]。配列 VALUE 復元 + 添字 c[0]/c[1] を検証 → 25v46f。
	SRAVA_SOURCE='var c = centroid(rect(2,3)); if (c[0] == 1) { if (c[1] == 1.5) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); } } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
centroid_3d)
	# 計測(配列返し): box(2,2,2) の体積重心=[1,1,1]。3 要素配列の添字 c[2] を検証 → 25v46f。
	SRAVA_SOURCE='var c = centroid(box(2,2,2)); if (c[2] == 1) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
distance)
	# 近接(値返し・二項): box [0,1]^3 と +3 平行移動した box の最近接距離=2(x=1 と x=3 の隙間)→ 25v46f。
	SRAVA_SOURCE='if (distance(box(1,1,1), box(1,1,1) >>> [3,0,0]) == 2) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
distance_err)
	# 近接は 3D 専用。2D 入力はエラー。
	SRAVA_SOURCE='export(distance(rect(1,1), rect(2,2)));' exec "$SRAVA" ;;
closest)
	# 近接(配列返し): [dist,[pa],[pb]]。dist=2 かつ pa.x=1(近接面)を**入れ子添字 c[1][0]** で検証 → 25v46f。
	SRAVA_SOURCE='var c = closest(box(1,1,1), box(1,1,1) >>> [3,0,0]); if (c[0] == 2) { if (c[1][0] == 1) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); } } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
farthest)
	# 近接(配列返し・頂点総当り厳密): 対角隅 (0,0,0)-(4,1,1) → √18≈4.24 > 4 → 25v46f。
	SRAVA_SOURCE='var c = farthest(box(1,1,1), box(1,1,1) >>> [3,0,0]); if (c[0] > 4) { export(box(2,2,2) ||| box(1,1,3)); } else { export(box(1,1,1)); }' exec "$SRAVA" ;;
tube)
	# 3D 掃引管: 直線パス 2 頂点・半径 0.5・八角断面。側面 8 帯 + 両端平キャップ = 18v32f。
	SRAVA_SOURCE='export(tube([[[0,0,0],0.5],[[0,0,3],0.5]], 8));' exec "$SRAVA" ;;
bigtube)
	# #4 性能崖の回帰ガード: 2048 点の巨大インライン配列(serialize 後 ~157KB > 64KB 既定パイプ)。
	# 修正前は planner→agent の pipe 送信が EAGAIN yield の resume 不全で停止(>40s〜ハング)。
	# 修正(agent stdin の F_SETPIPE_SZ 拡張)後は ~2s。TIMEOUT で崖の再発を検知する。
	PTS=$(python3 -c "import math;print(','.join('[[%g,%g,%g],0.3]'%(round(math.cos(i*0.05),4),round(math.sin(i*0.05),4),round(i*0.02,4)) for i in range(2048)))")
	SRAVA_SOURCE="export(tube([$PTS], 6));" exec "$SRAVA" ;;
tube_taper)
	# 太さ可変 + 端 r=0(尖り): 始端 apex(円錐)/終端 平キャップ。八角。10v16f。
	SRAVA_SOURCE='export(tube([[[0,0,0],0],[[0,0,3],0.5]], 8));' exec "$SRAVA" ;;
tube_union)
	# 管が閉多様体・外向き正しい証明: box との corefinement union が通る → 34v64f。
	SRAVA_SOURCE='export(box(2,2,2) ||| tube([[[0,0,0],0.5],[[0,0,4],0.5]], 8));' exec "$SRAVA" ;;
tube_dedup)
	# 連続重複頂点を弾かず間引く: 重複を含むパスでも、間引き後 2 頂点の素の管(18v32f)になる。
	SRAVA_SOURCE='export(tube([[[0,0,0],0.5],[[0,0,0],0.5],[[0,0,3],0.5]], 8));' exec "$SRAVA" ;;
tube2d)
	# 2D 次元ディスパッチ: 位置が [x,y] なら可変半幅の帯(cgMesh2D)。valid な単一領域になることを確認。
	SRAVA_SOURCE='var v = valid(tube([[[0,0],3],[[20,5],2],[[35,-8],4]])); if (v == 1) { print("TUBE2D_OK"); }' exec "$SRAVA" ;;
revolve)
	# 2D→3D 回転体: rect[0,1]x[0,2] を Y 軸 360° → 円柱(半径1高2)。軸接辺は潰れる。66v128f。
	SRAVA_SOURCE='export(revolve(rect(1,2), 360));' exec "$SRAVA" ;;
revolve_union)
	# 円柱が閉多様体・向き正しい証明: box との corefinement union が通る → 70v136f。
	SRAVA_SOURCE='export(box(5,5,5) ||| revolve(rect(1,2), 360));' exec "$SRAVA" ;;
revolve_partial)
	# 部分角(90°扇形柱)= 両端に CDT キャップ付き閉立体。20v36f。
	SRAVA_SOURCE='export(revolve(rect(1,2), 90));' exec "$SRAVA" ;;
revolve_segs)
	# 回転分割数(第3引数=回転ピッチ): 8 分割の粗い円柱 → 18v32f。
	SRAVA_SOURCE='export(revolve(rect(1,2), 360, 8));' exec "$SRAVA" ;;
revolve_partial_union)
	# 部分角が閉多様体・キャップ向き正しい証明: box union が通る → 22v40f。
	SRAVA_SOURCE='export(box(5,5,5) ||| revolve(rect(1,2), 90));' exec "$SRAVA" ;;
svg_roundtrip)
	# 2D SVG export→import round-trip。穴あき額縁が保たれて extrude=トンネル付き 16v32f。
	rm -f /tmp/srava-rt-test.svg
	SRAVA_SOURCE='export("/tmp/srava-rt-test.svg", rect(4,4) --- (rect(2,2) >>> [1,1,0])); export(extrude(import("/tmp/srava-rt-test.svg"), 1));' exec "$SRAVA" ;;
dxf_export)
	# 2D DXF export(LWPOLYLINE)。エラーにならず D_REF 出力。
	rm -f /tmp/srava-dxf-test.dxf
	SRAVA_SOURCE='export("/tmp/srava-dxf-test.dxf", ngon(6,1));' exec "$SRAVA" ;;
dxf_roundtrip)
	# DXF export→import round-trip。穴あき額縁が包含 nest で復元 → extrude トンネル付き 16v32f。
	rm -f /tmp/srava-rt-test.dxf
	SRAVA_SOURCE='export("/tmp/srava-rt-test.dxf", rect(4,4) --- (rect(2,2) >>> [1,1,0])); export(extrude(import("/tmp/srava-rt-test.dxf"), 1));' exec "$SRAVA" ;;
offset_inset)
	# 2D インセット(straight skeleton): rect(4,4) を -1 収縮 → 2x2 相当 → extrude 8v12f。
	SRAVA_SOURCE='export(extrude(offset(rect(4,4), -1), 1));' exec "$SRAVA" ;;
offset_shell)
	# 肉厚枠: offset(-1) を引いて幅1の枠 → 穴あき → extrude トンネル付き 16v32f。
	SRAVA_SOURCE='export(extrude(rect(4,4) --- offset(rect(4,4), -1), 1));' exec "$SRAVA" ;;
offset_vanish)
	# インセット過大 → 領域消滅(空)。extrude すると空メッシュ 0v0f。
	SRAVA_SOURCE='export(extrude(offset(rect(2,2), -5), 1));' exec "$SRAVA" ;;
offset_3d)
	# 3D オフセット = Minkowski(icosphere 球・既定 subdiv=1)。box を 0.5 膨張 → 滑らか角丸 72v140f。
	SRAVA_SOURCE='export(offset(box(2,2,2), 0.5));' exec "$SRAVA" ;;
offset_3d_coarse)
	# subdiv=0(粗い icosahedron 球)を明示 → 24v44f。球細分化レベルの制御。
	SRAVA_SOURCE='export(offset(box(2,2,2), 0.5, 0));' exec "$SRAVA" ;;
offset_3d_shell)
	# 3D 外殻: offset(box,0.3) --- box = 厚さ 0.3 の殻。3D corefinement に乗る → 32v56f。
	SRAVA_SOURCE='export(offset(box(2,2,2), 0.3) --- box(2,2,2));' exec "$SRAVA" ;;
offset_3d_inset)
	# 3D 収縮(補集合トリック): box(2,2,2)-0.3 → 痩せた箱(角は鋭いまま)8v12f。
	SRAVA_SOURCE='export(offset(box(2,2,2), -0.3));' exec "$SRAVA" ;;
offset_3d_inner_shell)
	# 内殻(中空箱): box --- offset(box,-0.5) = 厚さ 0.5 の殻 → 外箱8+内箱8 = 16v24f。
	SRAVA_SOURCE='export(box(3,3,3) --- offset(box(3,3,3), -0.5));' exec "$SRAVA" ;;
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
	SRAVA_SOURCE='var hollow = box(10,10,10) --- (box(6,6,12) >>> [2,2,-1]);
	print("SECAREA=", area(section(hollow, [0,0,5], [0,0,1])));' exec "$SRAVA" ;;
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
	SRAVA_SOURCE='export(box(2,2,2) +++ box(1,1,3));' exec "$SRAVA" ;;
combine_fn)
	# combine(a,b,c): n-ary も二項分解で合体。3 箱 = 24v36f。
	SRAVA_SOURCE='export(combine(box(2,2,2), box(1,1,3), box(3,1,1)));' exec "$SRAVA" ;;
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
	"$SRAVA" "$EF" 2>&1 | grep -E 'ERROR\[.*,4\] array index out of range' | head -1 ;;
errloc)
	# エラーの ERROR[file,line] 表示 + エラー時はキャッシュ掃除をしない(Feature1/2)。
	# 3 行目の volume(2D) がエラー。ファイル名と行番号、cleanup スキップを検証。
	EF="$D.sra"
	printf '// comment line 1\nvar a = box(1,1,1);\nexport(volume(rect(2,2)));\n' > "$EF"
	"$SRAVA" "$EF" 2>&1 | grep -E 'ERROR\[.*,3\]|exit cleanup: skipped' | head -2 ;;
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
	elif [ "$(printf '%s' "$OUT" | grep -o '\.cache' | wc -l)" = "2" ]; then echo "PMA_OK"
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
plugin_echo)
	# プラグインエージェント疎通: echo プラグイン($SRAVA_ECHO_AGENT)を別プロセスで起動し、
	# srava から plugin_echo(...) を呼んで構造化結果が返るか。マニフェストを一時 dir に生成。
	PD=$(mktemp -d)
	printf 'op = plugin_echo\nbin = %s\nout = value\n' "$SRAVA_ECHO_AGENT" > "$PD/echo.plugin"
	# native srava が読む PIG_PLUGIN_PATH は native 形に(MSYS /tmp と食い違うため)。Linux は no-op。
	PDW="$PD"; command -v cygpath >/dev/null 2>&1 && PDW=$(cygpath -m "$PD")
	OUT=$(PIG_PLUGIN_PATH="$PDW" SRAVA_SOURCE='var r = plugin_echo([1,2,3], "hi", 42); print("ECHO", r[1], r[0][2]);' "$SRAVA" 2>&1 | grep '^ECHO')
	rm -rf "$PD"
	# r[1]="hi", r[0][2]=3 → "ECHO hi 3"
	if [ "$OUT" = "ECHO hi 3" ]; then echo "PLUGIN_OK"; else echo "PLUGIN_FAIL: $OUT"; fi ;;
plugin_pipeprox)
	# pipe_proximity プラグイン: U 字に折り返すパイプ(両腕が接近)で自己接近を検出。
	# 太い半径(2.0)で reportGap 大きめにすると、折り返した腕どうしが接近 → 接近件数 > 0。
	PD=$(mktemp -d)
	printf 'op = pipe_proximity\nbin = %s\nout = value\n' "$SRAVA_PIPEPROX_AGENT" > "$PD/pp.plugin"
	OUT=$(PIG_PLUGIN_PATH="$PD" SRAVA_SOURCE='
	var pts = [[0,0,0],[10,0.5,0],[10,3,0],[0,3.5,0]];
	var hits = pipe_proximity(pts, [1.0, 0.0], 4.0);
	print("PP", length(hits) > 0);' "$SRAVA" 2>&1 | grep '^PP')
	rm -rf "$PD"
	if [ "$OUT" = "PP 1" ]; then echo "PIPEPROX_OK"; else echo "PIPEPROX_FAIL: $OUT"; fi ;;
plugin_pipeadjust)
	# pipe_adjust プラグイン op(同一 bin が pipe_proximity と両 serve): クリアランス違反の
	# 折り返しを端点固定で開き、gap >= dMin を満たす(feasible=1 かつ clearViolation≈0)。
	PD=$(mktemp -d)
	printf 'op = pipe_adjust\nbin = %s\nout = value\n' "$SRAVA_PIPEPROX_AGENT" > "$PD/pa.plugin"
	OUT=$(PIG_PLUGIN_PATH="$PD" SRAVA_SOURCE='
	var pts = [[0,0,0],[12,0,0],[10,2,0],[12,4,0],[0,4,0]];
	var res = pipe_adjust(pts, [0.8, 0.0], 0.6, 400, 1, 0.1);
	var ok = 0;
	if (res.feasible == 1) { if (res.clearViolation < 0.05) { ok = 1; } }
	print("PA", ok);' "$SRAVA" 2>&1 | grep '^PA')
	rm -rf "$PD"
	if [ "$OUT" = "PA 1" ]; then echo "PIPEADJUST_OK"; else echo "PIPEADJUST_FAIL: $OUT"; fi ;;
plugin_radius)
	# 半径プロファイル: スカラ一定 0.8 と、[s,r] キーポイント一定 0.8(クランプで全域 0.8)が
	# 同じ gap を返すこと(線形補間経路の健全性)。
	PD=$(mktemp -d)
	printf 'op = pipe_proximity\nbin = %s\nout = value\n' "$SRAVA_PIPEPROX_AGENT" > "$PD/pp.plugin"
	OUT=$(PIG_PLUGIN_PATH="$PD" SRAVA_SOURCE='
	var pts = [[0,0,0],[12,0,0],[10,2,0],[12,4,0],[0,4,0]];
	var a = pipe_proximity(pts, 0.8, 8.0)[0][0];
	var b = pipe_proximity(pts, [[0,0.8],[1000,0.8]], 8.0)[0][0];
	var d = a - b; if (d < 0) { d = 0 - d; }
	print("RAD", d < 0.0001);' "$SRAVA" 2>&1 | grep '^RAD')
	rm -rf "$PD"
	if [ "$OUT" = "RAD 1" ]; then echo "PIPERADIUS_OK"; else echo "PIPERADIUS_FAIL: $OUT"; fi ;;
plugin_scene)
	# N 体(Scene): 可動配管 body0 + 固定障害物 body1。近接検出(>0)し、adjustScene で
	# body0 を gap>=dMin へ調整(feasible=1 かつ clearViolation≈0)。同一 bin の scene 系 2 op。
	PD=$(mktemp -d)
	printf 'op = pipe_scene_proximity\nbin = %s\nout = value\n' "$SRAVA_PIPEPROX_AGENT" > "$PD/sp.plugin"
	printf 'op = pipe_scene_adjust\nbin = %s\nout = value\n'    "$SRAVA_PIPEPROX_AGENT" > "$PD/sa.plugin"
	OUT=$(PIG_PLUGIN_PATH="$PD" SRAVA_SOURCE='
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
	rm -rf "$PD"
	if [ "$OUT" = "SC 1" ]; then echo "PIPESCENE_OK"; else echo "PIPESCENE_FAIL: $OUT"; fi ;;
plugin_sample)
	# pipe_sample: 弧長等間隔サンプル。テーパ半径で先頭 r≈1.2(キーポイント厳密)・末尾<先頭、
	# 隣接間隔が指定ピッチ 1.5 にほぼ一致(弧長等間隔)。
	PD=$(mktemp -d)
	printf 'op = pipe_sample\nbin = %s\nout = value\n' "$SRAVA_PIPEPROX_AGENT" > "$PD/sm.plugin"
	OUT=$(PIG_PLUGIN_PATH="$PD" SRAVA_SOURCE='
	var pts = [[0,0,0],[12,0,0],[10,2,0],[12,4,0],[0,4,0]];
	var d = pipe_sample(pts, [[0,1.2],[100,0.4]], 1.5);
	var rf = d[0][1]; var rl = d[length(d)-1][1];
	var g = d[2][0] - d[1][0]; var p = sqrt(sum(g*g));
	var ok = 0;
	if (rf > 1.19) { if (rf < 1.21) { if (rl < rf) { if (p > 1.4) { if (p < 1.6) { ok = 1; } } } } }
	print("SM", ok);' "$SRAVA" 2>&1 | grep "^SM")
	rm -rf "$PD"
	if [ "$OUT" = "SM 1" ]; then echo "PIPESAMPLE_OK"; else echo "PIPESAMPLE_FAIL: $OUT"; fi ;;
plugin_separate)
	# 射影的分離パス: ピッチ≈2r で隣接ターンが接触する 2 周コイルを、pipe_adjust が
	# gap>=dMin へ押し広げる(energy 法では取れない重なり解消)。before≈0 → after≈dMin。
	PD=$(mktemp -d)
	printf 'op = pipe_proximity\nbin = %s\nout = value\n' "$SRAVA_PIPEPROX_AGENT" > "$PD/pp.plugin"
	printf 'op = pipe_adjust\nbin = %s\nout = value\n'    "$SRAVA_PIPEPROX_AGENT" > "$PD/pa.plugin"
	OUT=$(PIG_PLUGIN_PATH="$PD" SRAVA_SOURCE='
	include "std/math.sra";
	var ctr=6; var R=12; var r=4; var N=2;
	var sp=linspace(0,2*PI*N,N*ctr); var x=cos(sp)*R; var y=sin(sp)*R; var z=linspace(0,-2*r*N,N*ctr);
	var coil=transpose([x,y,z]);
	var bh=pipe_proximity(coil,[r,0],4*r); var before=999.0; if(length(bh)>0){before=bh[0][0];}
	var res=pipe_adjust(coil,[r,0],{dMin:0.5,fixEnds:0,maxIter:200});
	var ah=pipe_proximity(res.ctrl,[r,0],4*r); var after=999.0; if(length(ah)>0){after=ah[0][0];}
	var ok=0; if(before<0.2){ if(after>0.4){ ok=1; } } print("SEP",ok);' "$SRAVA" 2>&1 | grep "^SEP")
	rm -rf "$PD"
	if [ "$OUT" = "SEP 1" ]; then echo "PIPESEPARATE_OK"; else echo "PIPESEPARATE_FAIL: $OUT"; fi ;;
*)
	echo "unknown mode: $MODE"; exit 2 ;;
esac
