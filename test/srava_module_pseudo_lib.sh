#!/bin/sh
# 擬似モジュール集 lib/module/pseudo.sra — 粒度の既定値を練り込む (#3574)。
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR, SRAVA_PATH(=repo/lib)。
#
# ★ 何を守るテストか
#   ① pm_* は **[擬似, 実名] の組**を返す ⇒ use [pm_cgal({..})] の 1 個で
#      「擬似が粒度を埋める」と「残りの op は実モジュールが答える」の両方が揃う
#      (組が効くのは候補列が入れ子を平坦化するから = #3573 に直接乗っている)
#   ② seg が **練り込まれる** — sphere(r) が mod::sphere(r, seg) と同じ形になる
#      ★ 陰性対照: 擬似を外した同じ式は各カーネルの既定 (32) の形になる
#   ③ 明示した seg は **擬似を素通りして**そのまま効く (上書きしない)
#   ④ seg を渡さない ({}) なら **各カーネルの既定のまま** (擬似は形を変えない)
#   ⑤ `tube` → `mod::tube_ruled` の **名前の橋渡し**
#      ★ 陰性対照: 擬似が居なければ tube(path, 24) は「実行できるモジュールが無い」
#   ⑥ openvdb は **dx** を練り込む (seg を持つ op と持たない op が混ざる)
#   ⑦ 2D を持たないカーネルの擬似は circle/revolve を **宣言しない** ⇒ 隣へ降りる
#   ⑧ **知らないキー / ハッシュでない引数はエラー** (綴り違いが黙って既定値で通らない)
#   ⑨ pseudo.sra 自身は **use も module() も書かない** (既定を敷かない・#3555 の教訓)
#   ⑩ **pm_points** — 2 引数の intersection(点群, 形) を **3 要素配列**にする (#3575 (8))
#      ★★ ここだけ役割が違う: A/B 群は既定値を練り込むが、こちらは **引数の少ない形を足す**。
#      ★ 陰性対照: 擬似が居なければ 2 引数形は **落ちる** (関数形も `&&&` も)
#      ⚠⚠ **`&&&` でも通ること**を必ず見る — `a &&& b` は mk_meshop に落ちて mk_call を
#        通らないので、*パーサの糖衣では救えなかった* 綴り。ここが緑でないと、
#        「関数形だけ通る」という非対称が残ったまま誰も気づかない (bench の指摘 2026-09-22)。
#      ⚠ メッシュの intersection / `&&&` が **壊れていない**ことも同時に見る
#        (擬似が全 op に効くので、断り損ねるとブール積を奪う)
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
INC='include "module/all.sra"; include "module/pseudo.sra";'
PATH3='var path = [[[0,0,0],0.5],[[3,1,0],0.4],[[3,3,0],0.2]];'
NG=0
say() { echo "$1"; }
bad() { echo "FAIL($1)"; NG=1; }

run() {   # run <cache dir> <source>  → stdout そのまま
	rm -rf "$1"
	SRAVA_CACHE_DIR="$1" SRAVA_SOURCE="$2" "$SRAVA" 2>&1
}
val() { run "$1" "$2" | sed -n 's/^VAL //p'; }

# ---- ⓪ そもそも読めるか (ここが落ちると以降が全部同じ顔で落ちる) ----
o=$(run "$D-0" "include \"module/pseudo.sra\"; print(\"VAL\", 1);" | sed -n 's/^VAL //p')
[ "$o" = "1" ] || { bad "pseudo.sra を include できない"; run "$D-0" "include \"module/pseudo.sra\";" | head -3; }

# ---- ①② seg が練り込まれる / 組が効く ----
#   面数で見る (seg は面数を決めるので、どの seg で走ったかが値で分かる)。
F8=$(val "$D-a1"  "$INC print(\"VAL\", nfaces(\"cgal\"::sphere(1.5, 8)));")
FD=$(val "$D-a2"  "$INC print(\"VAL\", nfaces(\"cgal\"::sphere(1.5)));")
[ -n "$F8" ] && [ -n "$FD" ] && [ "$F8" != "$FD" ] || bad "前提が崩れた seg8='$F8' 既定='$FD'"
PB=$(val "$D-a3"  "$INC use [ pm_cgal({seg:8}) ]; print(\"VAL\", nfaces(sphere(1.5)));")
[ "$PB" = "$F8" ] || bad "pm_cgal({seg:8}) が seg を練り込まない '$PB' vs seg8='$F8'"
#   ★ 組の後半 (実名) が効いている証拠: nfaces は cgal の op なので、実名が列に
#     入っていなければ「どれも nfaces を持たない」で落ちる。上が通った時点で証明済み。
#   ★ 陰性対照: 擬似を外すと既定 (32) の形に戻る
NB=$(val "$D-a4"  "$INC use [ \"cgal\" ]; print(\"VAL\", nfaces(sphere(1.5)));")
[ "$NB" = "$FD" ] || bad "陰性対照: 擬似なしが既定でない '$NB' vs 既定='$FD'"
say "  1) [擬似, 実名] の組で seg を練り込む: 擬似あり=$PB 擬似なし=$NB (明示 seg8=$F8)"

# ---- ③ 明示した seg は素通り ----
F64=$(val "$D-b1" "$INC print(\"VAL\", nfaces(\"cgal\"::sphere(1.5, 64)));")
PE=$(val "$D-b2"  "$INC use [ pm_cgal({seg:8}) ]; print(\"VAL\", nfaces(sphere(1.5, 64)));")
[ "$PE" = "$F64" ] || bad "明示した seg=64 が擬似に上書きされた '$PE' vs '$F64'"
# ---- ④ {} なら各カーネルの既定のまま ----
PD=$(val "$D-b3"  "$INC use [ pm_cgal({}) ]; print(\"VAL\", nfaces(sphere(1.5)));")
[ "$PD" = "$FD" ] || bad "pm_cgal({}) が既定を変えた '$PD' vs 既定='$FD'"
say "  2) 明示 seg は素通り ($PE) ・ {} は既定のまま ($PD)"

# ---- ⑤ tube → tube_ruled の橋渡し ----
TR=$(val "$D-c1" "$INC $PATH3 print(\"VAL\", nfaces(\"cgal\"::tube_ruled(path, 24)));")
TB=$(val "$D-c2" "$INC $PATH3 use [ pm_cgal({seg:16}) ]; print(\"VAL\", nfaces(tube(path, 24)));")
[ -n "$TR" ] && [ "$TB" = "$TR" ] || bad "tube が tube_ruled へ橋渡しされない '$TB' vs '$TR'"
#   ★ 擬似の seg も効く (引数を省いたとき)
TS=$(val "$D-c3" "$INC $PATH3 use [ pm_cgal({seg:16}) ]; print(\"VAL\", nfaces(tube(path)));")
T16=$(val "$D-c4" "$INC $PATH3 print(\"VAL\", nfaces(\"cgal\"::tube_ruled(path, 16)));")
[ "$TS" = "$T16" ] || bad "tube(path) に擬似の seg=16 が効かない '$TS' vs '$T16'"
#   ★★ 陰性対照: 擬似が居なければ tube(path, 24) は実行できるモジュールが無い
#     (#3555 段4a で名前を分け、#3570 段3 で occt が第 2 引数ハッシュ限定になった結果)
o=$(run "$D-c5" "$INC $PATH3 print(nfaces(tube(path, 24)));" | grep -c "no module can execute op 'tube'")
[ "$o" -ge 1 ] || { bad "陰性対照: 擬似なしの tube(path,24) が落ちない (橋渡しの検定が空振り)"; \
	run "$D-c5" "$INC $PATH3 print(nfaces(tube(path, 24)));" | grep -i error | head -1; }
#   ★ occt の tube (第 2 引数がハッシュ) は擬似が居ても **奪われない**
OT=$(val "$D-c6" "$INC $PATH3 use [ pm_cgal({seg:16}), \"occt\" ];
print(\"VAL\", type_of(tube(path, {closed:0})));")
[ "$OT" = "oc-brep3d" ] || bad "occt の tube(path,{..}) を擬似が奪った '$OT' (want oc-brep3d)"
say "  3) tube → tube_ruled の橋渡し: 明示 24=$TB 擬似 16=$TS ・ occt の tube は無傷 ($OT)"

# ---- ⑥ openvdb は dx を練り込む ----
VD=$(val "$D-d1" "$INC print(\"VAL\", volume(\"openvdb\"::sphere(1.5, 0.05)));")
PV=$(val "$D-d2" "$INC use [ pm_openvdb({dx:0.05}) ]; print(\"VAL\", volume(sphere(1.5)));")
[ -n "$VD" ] && [ "$PV" = "$VD" ] || bad "pm_openvdb が dx を練り込まない '$PV' vs '$VD'"
#   ★ dx を持たない書き方 (box(2,2,2)) も通る = 引数の数が合うようになる
BV=$(val "$D-d3" "$INC use [ pm_openvdb({dx:0.05}) ]; print(\"VAL\", volume(box(2,2,2)));")
BD=$(val "$D-d4" "$INC print(\"VAL\", volume(\"openvdb\"::box(2,2,2,0.05)));")
[ -n "$BD" ] && [ "$BV" = "$BD" ] || bad "pm_openvdb の box が dx を足さない '$BV' vs '$BD'"
#   ★ seg と dx の両方を取る op (cylinder) は seg も練り込む
CV=$(val "$D-d5" "$INC use [ pm_openvdb({seg:8, dx:0.05}) ]; print(\"VAL\", volume(cylinder(1, 2)));")
CD=$(val "$D-d6" "$INC print(\"VAL\", volume(\"openvdb\"::cylinder(1, 2, 8, 0.05)));")
[ -n "$CD" ] && [ "$CV" = "$CD" ] || bad "pm_openvdb の cylinder が seg+dx を足さない '$CV' vs '$CD'"
#   ⚠ seg を持たない op に seg を渡す行は **置いていない** — sphere(r, 32) は擬似の行に
#     当たらず、実 openvdb へ降りて 32 を dx として読む (= 擬似が居ないときと同じ)。
#     ★ 「擬似が黙って 32 を捨てた」のではないことを、値が dx=32 のものと一致することで示す。
S32=$(val "$D-d7" "$INC use [ pm_openvdb({dx:0.05}) ]; print(\"VAL\", volume(sphere(1.5, 32)));")
R32=$(val "$D-d8" "$INC print(\"VAL\", volume(\"openvdb\"::sphere(1.5, 32)));")
[ "$S32" = "$R32" ] || bad "openvdb の sphere(r,32) が擬似に捕まった '$S32' vs 素='$R32'"
[ "$S32" != "$PV" ] || bad "sphere(r,32) が dx=0.05 と同じ = 擬似が seg を捨てている '$S32'"
say "  4) openvdb は dx を練り込む: sphere=$PV box=$BV cylinder=$CV ・ seg 付きは捕まえない"

# ---- ⑦ 2D を持たないカーネルは circle / revolve を宣言しない ----
#   geogram は circle を持たない ⇒ 擬似が受けてしまうと「隣へ降りる道」が塞がる。
CC=$(val "$D-e1" "$INC use [ pm_geogram({seg:8}), pm_cgal({seg:8}) ];
print(\"VAL\", nverts(circle(2)));")
C8=$(val "$D-e2" "$INC print(\"VAL\", nverts(\"cgal\"::circle(2, 8)));")
[ -n "$C8" ] && [ "$CC" = "$C8" ] || bad "geogram の擬似が circle を掴んで cgal へ降りない '$CC' vs '$C8'"
say "  5) 2D を持たない擬似は circle を宣言しない ⇒ 隣 (cgal) へ降りる: $CC 頂点"

# ---- ⑧ 引数の検査 ----
argerr() {   # argerr <id> <式> <期待する語>
	o=$(run "$D-f$1" "$INC use [ $2 ]; print(volume(box(2,2,2)));" | grep -c "$3")
	[ "$o" -ge 1 ] || { bad "$2 のエラー文に '$3' が出ない"; \
		run "$D-f$1" "$INC use [ $2 ]; print(volume(box(2,2,2)));" | grep -i error | head -1; }
}
#   ★ 綴り違いを黙って無視すると「指定したのに効かない」= 既定値で通る形になる
argerr 1 'pm_cgal({segs:8})'  '知らないキーがある'
argerr 2 'pm_cgal(8)'         'ハッシュで書く'
argerr 3 'pm_openvdb({dz:1})' '知らないキーがある'
#   ★ 陰性対照: 正しいキーは通る (検出器が「何にでも当たる」のではないこと)
OK1=$(val "$D-f4" "$INC use [ pm_openvdb({seg:8, dx:0.05}) ]; print(\"VAL\", volume(box(2,2,2)));")
[ -n "$OK1" ] || bad "陰性対照: 正しいキー {seg,dx} が通らない"
say "  6) 知らないキー / ハッシュでない引数はエラー (3 件 + 陰性対照)"

# ---- ⑨ pseudo.sra 自身は既定を敷かない ----
#   ★ include しただけで候補列が変わっていないこと (= use を書いていないこと) を
#     **振る舞いで**見る。⚠ ファイルを grep するだけでは「書いていない」しか言えない。
B0=$(val "$D-g1" "include \"module/all.sra\"; print(\"VAL\", volume(sphere(1.5)));")
B1=$(val "$D-g2" "$INC print(\"VAL\", volume(sphere(1.5)));")
[ -n "$B0" ] && [ "$B1" = "$B0" ] || bad "pseudo.sra を include すると既定の答えが変わる '$B1' vs '$B0'"
#   ★ module() も書いていない: all.sra 抜きで include して、ロード本数が 0 のままか
o=$(run "$D-g3" "include \"module/pseudo.sra\"; print(\"VAL\", length(modules()));" | sed -n 's/^VAL //p')
[ "$o" = "0" ] || bad "pseudo.sra が .so をロードしている (modules()=$o・0 のはず)"
say "  7) include しても既定は変わらず (.so も 0 本): 既定=$B0"

# ---- ⑩ pm_points — 2 引数の intersection を 3 要素配列にする (#3575 (8)) ----
#   点は箱 [0,2]^3 に対して: 内側 2 / 境界 1 / 外側 2 になるように選んである。
#   ⚠ manifold の box は **原点が角** (bbox=[[0,0,0],[2,2,2]])。中心だと思って書くと
#     内側のつもりの点が境界に載る (2026-09-22 に実際に取り違えた)。
PTS='var p = points3d([[1,1,1],[0.5,0.5,0.5],[0,1,1],[5,5,5],[9,9,9]]); var m = box(2,2,2);'
USEP='use [ pm_points({}), "geomutils", "manifold", "points" ];'

# ★ 陰性対照 — **擬似が居なければ 2 引数形は落ちる**。
#   ⚠ これを先に見ないと、後の「通った」が *擬似のおかげで通った* ことの証拠にならない。
o=$(run "$D-p0" "$INC $PTS print(\"VAL\", nverts(intersection(p, m)));")
case "$o" in *"no module can execute op 'intersection'"*) ;; *) bad "陰性対照: 擬似なしの 2 引数形が落ちない: $(echo "$o" | head -1)" ;; esac
o=$(run "$D-p1" "$INC $PTS print(\"VAL\", nverts(p &&& m));")
case "$o" in *"no module can execute op 'intersection'"*) ;; *) bad "陰性対照: 擬似なしの p &&& m が落ちない: $(echo "$o" | head -1)" ;; esac
say "  8) 陰性対照: 擬似が居なければ 2 引数形は落ちる (関数形 / &&& とも)"

# ★ 関数形 — 3 要素配列・分割の不変条件。
o=$(val "$D-p2" "$INC $USEP $PTS var s = intersection(p, m);
print(\"VAL\", length(s), nverts(s[0]), nverts(s[1]), nverts(s[2]),
      nverts(s[0]) + nverts(s[1]) + nverts(s[2]), nverts(p));")
[ "$o" = "3 1 2 2 5 5" ] || bad "pm_points の関数形が [3, 境界1, 内側2, 外側2, 合計5, 元5] でない: '$o'"

# ★★ 演算子形 — **ここが本題**。糖衣では救えなかった綴り。
o=$(val "$D-p3" "$INC $USEP $PTS var s = p &&& m;
print(\"VAL\", length(s), nverts(s[0]), nverts(s[1]), nverts(s[2]));")
[ "$o" = "3 1 2 2" ] || bad "pm_points が p &&& m で効いていない: '$o'"
say "  9) pm_points: 2 引数の intersection が 3 要素配列 (関数形 / &&& とも・合計=元の点数)"

# ⚠ メッシュのブール演算を **奪っていない**こと (擬似は全 op に効くので断り損ねると壊れる)。
o=$(val "$D-p4" "$INC $USEP var a = box(2,2,2); var b = translate(box(2,2,2),[1,0,0]);
print(\"VAL\", volume(a &&& b), volume(intersection(a, b)), volume(a ||| b), volume(a --- b));")
[ "$o" = "4 4 12 4" ] || bad "擬似がメッシュのブール演算を奪った (期待 '4 4 12 4'): '$o'"

# ★ 3 引数形は擬似を **素通り**して実モジュールへ (arity で外れる = 擬似自身へ再帰しない)。
#   ⚠ ここが再帰すると *返ってこない* ので、緑であること自体が「戻らない」の証拠。
o=$(val "$D-p5" "$INC $USEP $PTS print(\"VAL\", nverts(intersection(p, m, -1)), nverts(difference(p, m)), nverts(p --- m));")
[ "$o" = "2 2 2" ] || bad "3 引数形 / difference が擬似を素通りしていない (期待 '2 2 2'): '$o'"
say " 10) 擬似はメッシュのブールを奪わず / 3 引数形と difference は素通り"

[ "$NG" -eq 0 ] && echo "MODULE-PSEUDO-LIB-OK"
exit "$NG"
