#!/bin/sh
# #3595 の応用 — **ライブラリ関数は自分の候補列を関数の頭で宣言する** (ひさ 2026-09-24)。
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR, SRAVA_PATH (lib/ を指すこと)。
#
# ★ 何を守るテストか
#   ① 宣言に書かれた名前が **実在のモジュール**であること (綴り / 改名で腐るのを機械で留める)
#      ⚠ 名前は lib/std/*.sra から **grep で起こす** — 手で並べると本体と二重帳簿になる
#   ② ライブラリ関数は `use mod_only(sup);` の 1 行で宣言する。契約は 3 つ:
#        ・呼び手が選んだカーネルが sup に在る  → **それで解く** (選択を殺さない)
#        ・呼び手が **何も言っていない**        → **載っているもの** (modules()) ∩ sup から選ぶ
#        ・呼び手の選択が sup と **交差しない**  → **エラー** (黙って独断で解かない)
#      ★ 陰性対照: 宣言を持たない関数は呼び手の列を **そのまま**引き継ぐ (apply の動的引き継ぎ)
#   ③ 必須 (`module([…],{})`) は **呼んだ時点で自動ロード**される (tube_fw = occt 専用 op)
#   ④ 1 本も載っていなければ **use の行**で落ちる (どの .so を忘れたかが op より前に分かる)
#   ⑤ 関数を抜ければ **呼び手の列へ戻る** (use が DEF であること)
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
LIB="$(dirname "$0")/../lib"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
NG=0
say() { echo "$1"; }
bad() { echo "FAIL($1)"; NG=1; }

run() {   # run <cache dir> <source> → stdout そのまま
	rm -rf "$1"
	SRAVA_CACHE_DIR="$1" SRAVA_CACHE_RETAIN=all SRAVA_SOURCE="$2" "$SRAVA" 2>&1
}
val() { run "$1" "$2" | sed -n 's/^VAL //p'; }

# ---- ① 宣言に出る名前が実在するか (lib/std から grep で起こす) ----
#   ⚠⚠ 抽出は **宣言の書き方に追従させること**。列を `var sup = [...]` に括り出した時点で
#     `use [...]` だけを見る抽出器は **9 個 → 2 個に黙って減った** (2026-09-24 に踏んだ)。
#     ⇒ 両方から拾い、さらに **下限本数**で「痩せたら赤」にしてある (数が減る事故は文言では防げない)。
NAMES=$(grep -ho -e 'use \[[^]]*\]' -e 'var sup *= *\[[^]]*\]' "$LIB"/std/*.sra |
        grep -o '"[a-z_0-9]*"' | tr -d '"' | sort -u)
N_NAMES=$(echo $NAMES | wc -w)
[ "$N_NAMES" -ge 5 ] || bad "宣言から拾えた名前が $N_NAMES 個しかない (抽出器が宣言の書き方に追従していない)"
LIST=$(echo "$NAMES" | sed 's/^/"/; s/$/"/' | tr '\n' ',' | sed 's/,$//')
o=$(run "$D-n1" "module([$LIST], {}); print(\"OK\");")
echo "$o" | grep -q '^OK$' || { bad "宣言に実在しない名前がある: $(echo $NAMES)"; echo "$o" | grep -i error | head -2; }
say "  1) 宣言の名前 $N_NAMES 個はすべて実在のモジュール: $(echo $NAMES | tr '\n' ' ')"

# ---- ①-b 宣言の **網羅** — モジュール op を直接呼ぶ関数は必ず use を持つ ----
#   ★ これが無いと「宣言し忘れた関数」が黙って残る (2026-09-24 に roll.sra の respace / solve で
#     実際に踏んだ: pipe_sample / pipe_scene_adjust は pipe_proximity 専用なのに宣言が無かった)。
#   ⚠ 判定材料の op 名は **which() に訊く** (手で並べると本体と二重帳簿になる)。
#   ⚠ 演算子形 (`+++` = combine ・ `>>>` = transform) はこの検査では見えない — 名前で書かれた
#     呼び出しだけが対象。⇒ 「抜けが無い」ではなく「**名前で呼んでいる分に抜けが無い**」。
IDS=$(cat "$LIB"/std/*.sra | sed 's|//.*||' | grep -o '[a-z_][a-z_0-9]*(' | tr -d '(' | sort -u |
      sed 's/^/"/; s/$/"/' | tr '\n' ',' | sed 's/,$//')
OPS=$(run "$D-n2" "module([\"cgal\",\"manifold\",\"occt\",\"geogram\",\"cherchi\",\"openvdb\",\"nef_hybrid\",\"geomutils\",\"points\",\"pipe_proximity\"],{optional:1});
var ids = [$IDS]; var i;
for (i=0;i<length(ids);i=i+1) { if ( which(ids[i]) != \"\" ) { print(\"OP\", ids[i]); } }" |
      sed -n 's/^OP //p' | sort -u)
[ -n "$OPS" ] || bad "モジュール op が 1 つも拾えない (which() の照会が空振り = 検査になっていない)"
MISS=$(for f in "$LIB"/std/*.sra; do
	awk -v ops="$OPS" -v file="$f" '
		BEGIN { n = split(ops, O, "\n") }
		/^var [A-Za-z_][A-Za-z_0-9]* = \\\(/ { fn = $2; has_use = 0; hit = "" }
		fn != "" && /^[ \t]*use / { has_use = 1 }
		fn != "" {
			line = $0; sub(/\/\/.*/, "", line)
			for ( i = 1 ; i <= n ; i++ )
				if ( O[i] != "" && line ~ ("[^a-z_0-9]" O[i] "[ \t]*\\(") ) hit = O[i]
		}
		fn != "" && /^};/ {
			if ( hit != "" && has_use == 0 ) printf "%s:%s (op %s)\n", file, fn, hit
			fn = ""
		}' "$f"
done)
[ -z "$MISS" ] || { bad "モジュール op を呼ぶのに use 宣言が無い関数がある"; echo "$MISS" | sed 's/^/    /'; }
say "  1b) モジュール op を名前で呼ぶ関数はすべて use を持つ (op は which() から起こした)"

# ---- ②⑤ 呼び手の列に引きずられない / 抜ければ戻る ----
#   ★ 球は occt が厳密なのでメッシュ系と型が違う ⇒ type_of でどちらが走ったか分かる。
#   ⚠ 値では判別できない (cgal と manifold は共通生成器で一致する) ので **型**で見る。
MOD='module("cgal.so",{}); module("manifold.so",{}); module("occt.so",{});'
#   ②-a 呼び手が **sup に無い** カーネル (occt) を選ぶ ⇒ **エラー** (独断で解かない)
#        ★ 文言は mod_only 由来のものになること (「空の列を書いた」ではないと言い分ける)
o=$(run "$D-a1" "$MOD use [\"occt\"];
include \"std/guide.sra\";
print(type_of(ruler(0, 20, 10, 0.3)));")
echo "$o" | grep -q "none of the modules in effect is supported here" ||
	{ bad "交差しない選択で mod_only 由来の診断が出ない"; echo "$o" | grep -i error | head -2; }
#   ②-b 呼び手が **sup に在る** カーネル (manifold) を選ぶ ⇒ ライブラリも manifold で解く
#        ⚠ これが無いと「宣言 = 呼び手の選択を殺す」形でも ②-a は緑のままになる
MF=$(val "$D-a3" "$MOD use [\"manifold\"];
include \"std/curve.sra\";
print(\"VAL\", type_of(tube_fw_ruled([[0,0,0],[10,0,0]], 2.0)));")
[ "$MF" = "mf-mesh3d" ] || bad "呼び手が選んだ manifold が尊重されない (mod_only が効いていない) '$MF'"
#   ②-c 呼び手が何も敷かなければ **載っているもの** から (= 従来どおり cgal)
#        ⚠ ここが赤くなると「use を書かない既存スクリプトが動かない」ことを意味する
DF=$(val "$D-a4" "$MOD include \"std/curve.sra\";
print(\"VAL\", type_of(tube_fw_ruled([[0,0,0],[10,0,0]], 2.0)));")
[ "$DF" = "cg-mesh3d" ] || bad "呼び手が何も敷かないときに載っているものから選べていない '$DF'"
#   ②-d 対照: cgal を積まない構成では **載っているもの** が変わるので答えも変わる
MO=$(val "$D-a5" "module(\"manifold.so\",{}); include \"std/curve.sra\";
print(\"VAL\", type_of(tube_fw_ruled([[0,0,0],[10,0,0]], 2.0)));")
[ "$MO" = "mf-mesh3d" ] || bad "載っているものに追随していない (cgal 抜きで manifold にならない) '$MO'"
#   ★ 陰性対照 — 宣言が無ければ同じ形で **引きずられる** (①②が検定になっていることの証拠)
cat > "$D-nodecl.sra" <<'EOF'
var ruler_nodecl = \(axis, len, step, r) { tube_ruled([[[0,0,0], r], [[len,0,0], r]]); };
EOF
NC=$(run "$D-a2" "$MOD include \"$D-nodecl.sra\"; use [\"occt\"];
print(\"VAL\", type_of(ruler_nodecl(0, 20, 10, 0.3)));" | sed -n 's/^VAL //p')
[ "$NC" = "oc-brep3d" ] || bad "陰性対照が成立しない (宣言なしの関数が呼び手の列を引き継がない) '$NC'"
say "  2) 選択を尊重 (manifold) / 無宣言は載っているもの / 交差なしはエラー / 抜けると戻る"

# ---- ③ 必須は自動ロードされる ----
T=$(val "$D-b1" 'include "std/curve.sra";
print("VAL", type_of(tube_fw([[0,0,0],[10,0,0],[10,10,0]], 2.0)));')
[ "$T" = "oc-brep3d" ] || bad "tube_fw が occt を自動ロードしない (必須の宣言が効いていない) '$T'"
say "  3) 必須 module([\"occt\"],{}) は呼んだ時点で自動ロードされる"

# ---- ④ 1 本も載っていなければ use の行で落ちる ----
#   ★ 1 引数形なので、左辺は modules() = **空** になり「対応集合と交差しない」側の文言が出る。
#     どちらにせよ **op に到達する前**に、ライブラリの行番号つきで落ちることを見る。
o=$(run "$D-c1" 'include "std/curve.sra";
print(volume(tube_fw_ruled([[0,0,0],[10,0,0]], 2.0)));')
echo "$o" | grep -q "none of the modules in effect is supported here" ||
	{ bad "1 本も無いのに use の行で落ちない"; echo "$o" | grep -i error | head -2; }
#   ⚠ 区切りは機種で違う (`lib/std/curve.sra` 対 `lib\std\curve.sra`) ので **ファイル名と行番号**で見る。
#     見たいのは「*ライブラリ側の宣言の行*を指しているか」であって、パスの綴りではない。
echo "$o" | grep -qE "curve\.sra,[0-9]+" ||
	{ bad "エラーが宣言の行 (curve.sra の行番号) を指していない"; echo "$o" | grep -i error | head -1; }
say "  4) 1 本も載っていなければ use の行で落ちる (ライブラリの行番号つき)"

[ "$NG" -eq 0 ] && echo "LIB-USE-DECL-OK"
exit "$NG"
