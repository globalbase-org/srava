#!/bin/sh
# 擬似モジュール — srava のラムダへ配線する候補 (#3555 段3)。
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ 何を守るテストか
#   ① 擬似 op が当たり、body の返り値がそのまま式の値になる
#      ★ 陰性対照: 同じ式を擬似抜きで回すと実カーネルが答える
#   ② **順序は候補列そのもの** — [p,"cgal"] は擬似 / ["cgal",p] は実 / p:: 単体も指名になる
#   ③ in[] の **述語ラムダ**が引数の中身を見て行を選ぶ (実 op の AK_MATCH に当たるもの)
#      偽なら次の候補へ落ちる / 同名 2 行は **頭から先勝ち**
#   ④ 用途: cast にできない変換 (粒度の既定値が要る) を cast 同然に使う
#      ★ 型は **返ってきた値が自分で持っている型**。sig の宣言ではない
#   ⑤ **キャッシュを作らない** — body を直接書いたときと miss 数が同じ
#   ⑥ ★★ 「**壊れている**」と「**選ばれない**」を分ける (#3555 の欄仕様・2026-09-21)
#      壊れている = 欄が無い / 綴りが違う / body の引数の数が in[] と食い違う → **明示エラー**
#                   (登録の口が無く、記述子のロード時検査に当たるものが無いため)
#      選ばれない = 引数の数 / 種別 / 述語 / sig が合わない → **次の行 → 黙って routing へ降りる**
#   ⑦ sig の省略は 3 綴り (**無い / "" / []**) とも「入力型で絞らない」で同じ
#   ⑧ nreq で省略できる。足りない末尾は **null で埋まる** ⇒ body は `== null` で読む (#3567)
#   ⑨ 同名 2 行を **引数の数**で振り分けられる (以前は 1 行目で止まっていた)
#   ⑩ 札 (欄 name) がエラー文に出る。⚠ 実モジュールと同名なら {pseudo} に倒す
#   ⑪ vtail は "value" / "cache" の文字列・既定 "cache"
#   ⑫ どの候補も引数の数を受けられないときは **そう言う** (候補を全部並べる)
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
MOD='module("cgal.so",{}); module("manifold.so",{}); module("occt.so",{}); module("occt_mf.so",{});'
NG=0
say() { echo "$1"; }
bad() { echo "FAIL($1)"; NG=1; }

run() { rm -rf "$1"; SRAVA_CACHE_DIR="$1" SRAVA_CACHE_RETAIN=all SRAVA_SOURCE="$2" "$SRAVA" 2>&1; }
val() { run "$1" "$2" | sed -n 's/^VAL //p'; }
miss() { run "$1" "$2" | sed -n 's/.*cache: [0-9]* hit(s), \([0-9]*\) miss(es).*/\1/p'; }

# 42 を返すだけの擬似 volume。⚠ srava のラムダ本体は文なので `1;` のように `;` が要る。
P42='var p = { type : "pseudo_module",
                ops  : [ { name : "volume", in : ["cache"], sig : ["(cg-mesh3d)->value"],
                           body : \(g){ 42; } } ] };'

# ---- ①② 当たる / 順序が候補列そのもの ----
A=$(val "$D-p1" "$MOD $P42 var u = [p,\"cgal\"];  print(\"VAL\", u::volume(box(2,2,2)));")
B=$(val "$D-p2" "$MOD $P42 var u = [\"cgal\",p];  print(\"VAL\", u::volume(box(2,2,2)));")
C=$(val "$D-p3" "$MOD $P42 print(\"VAL\", p::volume(box(2,2,2)));")
N=$(val "$D-p4" "$MOD $P42 print(\"VAL\", volume(box(2,2,2)));")
[ "$A" = "42" ] || bad "擬似が先頭なのに当たらない '$A'"
[ "$B" = "8" ]  || bad "実モジュールが先頭なのに擬似が勝っている '$B'"
[ "$C" = "42" ] || bad "ハッシュ単体の指名が効かない '$C'"
[ "$N" = "8" ]  || bad "陰性対照: 擬似抜きで実カーネルが答えない '$N'"
say "  1) 擬似=$A 実=$B ハッシュ単体=$C 陰性対照=$N"

# ---- ③ 述語ラムダ ----
PSEL='var p = { type : "pseudo_module", ops : [
        { name : "scale", in : ["cache", \(k){ k > 10; }], sig : ["(cg-mesh3d)->value"],
          body : \(g,k){ 111; } },
        { name : "scale", in : ["cache", "value"],         sig : ["(cg-mesh3d)->value"],
          body : \(g,k){ 222; } } ] };'
BIG=$(val "$D-p5" "$MOD $PSEL var u = [p,\"cgal\"]; print(\"VAL\", u::scale(box(2,2,2), 50));")
SML=$(val "$D-p6" "$MOD $PSEL var u = [p,\"cgal\"]; print(\"VAL\", u::scale(box(2,2,2), 3));")
[ "$BIG" = "111" ] || bad "述語が真の行に当たらない '$BIG'"
[ "$SML" = "222" ] || bad "述語が偽でも 1 行目に当たっている '$SML'"
#   述語が偽で、後続の擬似行も無ければ **次の候補 (実モジュール)** へ落ちる
FT=$(val "$D-p7" "$MOD var p = { type:\"pseudo_module\", ops:[
        { name:\"volume\", in:[\\(g){ 0; }], sig:[\"(cg-mesh3d)->value\"], body:\\(g){ 99; } } ] };
     var u = [p,\"cgal\"]; print(\"VAL\", u::volume(box(2,2,2)));")
[ "$FT" = "8" ] || bad "述語が偽なのに次の候補へ落ちない '$FT'"
#   op 名が別なら素通り
PT=$(val "$D-p8" "$MOD var p = { type:\"pseudo_module\", ops:[
        { name:\"nosuchop\", in:[\"cache\"], sig:[\"(cg-mesh3d)->value\"], body:\\(g){ 1; } } ] };
     var u = [p,\"cgal\"]; print(\"VAL\", u::volume(box(2,2,2)));")
[ "$PT" = "8" ] || bad "宣言していない op 名で擬似が当たっている '$PT'"
say "  2) 述語が値の中身で行を選ぶ (>10 → $BIG / それ以外 → $SML)・偽なら次の候補へ"

# ---- ④⑤ 用途: cast にできない変換を cast 同然に ----
#   occt の厳密な立体 → mf-mesh3d。粒度 (defl) が要るので **cast では書けない** 変換。
TRI='var defl = 0.05;
     var tri = { type : "pseudo_module",
                 ops  : [ { name : "cast", in : ["value","cache"],
                            sig  : ["(oc-brep3d)->mf-mesh3d"],
                            body : \(t,g){ triangulate(g, defl); } } ] };
     var u = [tri, "cgal", "manifold", "occt"];
     var s = "occt"::sphere(1.5);'
OUT=$(run "$D-p9" "$MOD $TRI var m = u::cast(\"mf-mesh3d\", s);
print(\"VAL\", type_of(m)); print(\"VAL\", \"manifold\"::volume(m));")
TY=$(echo "$OUT" | sed -n 's/^VAL //p' | sed -n 1p)
VO=$(echo "$OUT" | sed -n 's/^VAL //p' | sed -n 2p)
[ "$TY" = "mf-mesh3d" ] || { bad "擬似 cast の結果の型が違う '$TY'"; echo "$OUT" | grep -i error | head -1; }
[ -n "$VO" ] || bad "擬似 cast の結果から体積が出ない"
#   ★ 型は **値が自分で持っているもの**: 同じ式を body の中身で直接書いたら同じ型・同じ値
DIR=$(run "$D-pa" "$MOD $TRI var m = triangulate(s, defl);
print(\"VAL\", type_of(m)); print(\"VAL\", \"manifold\"::volume(m));" | sed -n 's/^VAL //p')
[ "$(echo "$OUT" | sed -n 's/^VAL //p')" = "$DIR" ] || \
	{ bad "擬似 cast と body 直書きで結果が違う"; echo "$DIR"; }
#   ★ キャッシュを作らない: miss 数が body 直書きと **同じ**
M1=$(miss "$D-pb" "$MOD $TRI print(\"VAL\", \"manifold\"::volume(u::cast(\"mf-mesh3d\", s)));")
M2=$(miss "$D-pc" "$MOD $TRI print(\"VAL\", \"manifold\"::volume(triangulate(s, defl)));")
[ -n "$M1" ] && [ "$M1" = "$M2" ] || bad "擬似 op がキャッシュを作っている miss=$M1 (直書き $M2)"
say "  3) cast にできない変換を cast 同然に: type_of=$TY vol=$VO ・ miss $M1 = 直書き $M2"

# ---- ⑥ 定義の誤りは飛ばさずエラー ----
pserr() {   # pserr <id> <ops の中身> <期待する語>
	o=$(run "$D-pe$1" "$MOD var p = { type:\"pseudo_module\", ops:[ $2 ] };
	     var u = [p,\"cgal\"]; print(u::volume(box(2,2,2)));" | grep -c "$3")
	[ "$o" -ge 1 ] || { bad "定義の誤りが出ない ($2 → '$3')"; \
		run "$D-pe$1" "$MOD var p = { type:\"pseudo_module\", ops:[ $2 ] };
		     var u = [p,\"cgal\"]; print(u::volume(box(2,2,2)));" | grep -i error | head -1; }
}
# ★ 壊れている = 明示エラー
pserr 1 '{ in:["cache"], sig:["(cg-mesh3d)->value"], body:\(g){ 1; } }'       "has no 'name'"
pserr 2 '{ name:"volume", in:["cache"], sig:["(cg-mesh3d)->value"] }'         "has no lambda 'body'"
pserr 3 '{ name:"volume", sig:["(cg-mesh3d)->value"], body:\(g){ 1; } }'      "has no 'in'"
pserr 4 '{ name:"volume", in:["mesh"], body:\(g){ 1; } }'                     'must be "value", "cache" or a match lambda'
pserr 5 '{ name:"volume", in:["cache"], body:\(g,k){ 1; } }'                  "body takes 2 argument(s) but 'in' declares 1"
pserr 6 '{ name:"volume", in:["cache"], nreq:5, body:\(g){ 1; } }'            'outside 0..1'
pserr 7 '{ name:"volume", in:["cache"], vtail:"mesh", body:\(g){ 1; } }'      'must be "value" or "cache"'
say "  4) 壊れている定義は **飛ばさず**エラー (7 件)"

# ---- ⑥b 選ばれないだけなら **黙って routing へ降りる** (エラーにしない) ----
#   ★★ ここが (b) の肝。以前は「当たった行の引数が合わない」を *エラー* にしていたので、
#     同名 2 行で数や種別を振り分けることができなかった。
psdown() {   # psdown <id> <ops の中身> <期待値>  … 実カーネル (cgal) が答えれば降りた証拠
	o=$(val "$D-pd$1" "$MOD var p = { type:\"pseudo_module\", ops:[ $2 ] };
	     var u = [p,\"cgal\"]; print(\"VAL\", u::volume(box(2,2,2)));")
	[ "$o" = "$3" ] || bad "降りない / 値が違う ($2 → '$o' ・ 期待 '$3')"
}
#   種別が合わない (mesh を渡したが in は value)  → 降りる
psdown 1 '{ name:"volume", in:["value"], body:\(g){ 1; } }'                    '8'
#   引数の数が合わない (1 個渡したが in は 2 個)   → 降りる
psdown 2 '{ name:"volume", in:["cache","value"], body:\(g,k){ 1; } }'          '8'
#   sig が当たらない                               → 降りる
psdown 3 '{ name:"volume", in:["cache"], sig:["(oc-brep3d)->value"], body:\(g){ 1; } }' '8'
#   ★ 陽性対照: 同じ形で sig だけ当てれば擬似が答える (上の 3 件が「そもそも当たらない」のではない)
psdown 4 '{ name:"volume", in:["cache"], sig:["(cg-mesh3d)->value"], body:\(g){ 1; } }' '1'
say "  5) 選ばれないだけなら黙って routing へ降りる (3 件 + 陽性対照)"

# ---- ⑦ sig の省略は 3 綴りとも「絞らない」 ----
for sp in 'sig:[], ' 'sig:"", ' ''; do
	o=$(val "$D-ps$(echo "$sp" | wc -c)" "$MOD
	     var p = { type:\"pseudo_module\",
	               ops:[ { name:\"volume\", in:[\"cache\"], ${sp}body:\(g){ 7; } } ] };
	     var u = [p,\"cgal\"]; print(\"VAL\", u::volume(box(2,2,2)));")
	[ "$o" = "7" ] || bad "sig の綴り '${sp}' で絞らない扱いになっていない '$o'"
done
say "  6) sig は 無い / \"\" / [] の 3 綴りとも「入力型で絞らない」"

# ---- ⑧⑨⑪⑫ ここから下は `simplify` を使う ----
#   ⚠⚠ `volume` は **パーサが第 1 引数しか積まず、残りを黙って捨てる**
#     (`volume(box(2,2,2), 9)` がエラーにならず 8 を返す ・ ns_sravaParser.y の measure 系)。
#     ⇒ 引数の数を見る検定には使えない。これは #3570 が直す対象そのもの。
#     `simplify` は全引数を積む側の op なので、擬似の in[] と突き合わせられる。
#   ★ 降り先は cgal の simplify (2 引数)。

# ---- ⑧ nreq と null 埋め ----
#   ★ body は `== null` で「省略された」を読む。⚠ 真偽 `if (k)` では **0 を渡した人も**
#     省略扱いになるので、ここは #3567 の null リテラルが要る。
PN='var p = { type:"pseudo_module",
             ops:[ { name:"simplify", in:["cache","value"], nreq:1,
                     body:\(g,k){ if (k == null) { 100; } else { 200 + k; } } } ] };'
O1=$(val "$D-pn1" "$MOD $PN var u=[p,\"cgal\"]; print(\"VAL\", u::simplify(box(2,2,2)));")
O2=$(val "$D-pn2" "$MOD $PN var u=[p,\"cgal\"]; print(\"VAL\", u::simplify(box(2,2,2), 5));")
O3=$(val "$D-pn3" "$MOD $PN var u=[p,\"cgal\"]; print(\"VAL\", u::simplify(box(2,2,2), 0));")
[ "$O1" = "100" ] || bad "nreq で省略した末尾が null で埋まらない '$O1'"
[ "$O2" = "205" ] || bad "省略しない呼びが通らない '$O2'"
[ "$O3" = "200" ] || bad "**0 を渡した**のに省略扱いになっている '$O3' (真偽で見ている)"
say "  7) nreq の省略が null で埋まり、0 を渡した呼びと区別できる ($O1 / $O2 / $O3)"

# ---- ⑨ 同名 2 行を引数の数で振り分ける ----
#   ★★ 以前は 1 行目で止まり、どちらの順に書いても片方だけが通った (行の選択に arity が無かった)。
P2R='var p = { type:"pseudo_module",
              ops:[ { name:"simplify", in:["cache"],         body:\(g){ 11; } },
                    { name:"simplify", in:["cache","value"], body:\(g,k){ 22; } } ] };'
R1=$(val "$D-pr1" "$MOD $P2R var u=[p,\"cgal\"]; print(\"VAL\", u::simplify(box(2,2,2)));")
R2=$(val "$D-pr2" "$MOD $P2R var u=[p,\"cgal\"]; print(\"VAL\", u::simplify(box(2,2,2), 9));")
[ "$R1" = "11" ] || bad "1 引数が 1 行目に振られない '$R1'"
[ "$R2" = "22" ] || bad "2 引数が 2 行目に振られない '$R2' (1 行目で止まっている)"
#   ★ 逆順に書いても同じ (先勝ちではなく **数で選んでいる**ことの陰性対照)
P2S='var p = { type:"pseudo_module",
              ops:[ { name:"simplify", in:["cache","value"], body:\(g,k){ 22; } },
                    { name:"simplify", in:["cache"],         body:\(g){ 11; } } ] };'
R3=$(val "$D-pr3" "$MOD $P2S var u=[p,\"cgal\"]; print(\"VAL\", u::simplify(box(2,2,2)));")
[ "$R3" = "11" ] || bad "行の順を変えると振り分けが崩れる '$R3'"
say "  8) 同名 2 行を引数の数で振り分ける (1 個→$R1 / 2 個→$R2 ・ 順不同)"

# ---- ⑩ 札 (欄 name) ----
#   ★ 札はエラー文に出る。⚠ **指名には使えない**ので、実モジュールと同名なら {pseudo} に倒す。
PG3='{ name:"simplify", in:["cache","value","value"], body:\(a,b,c){ 1; } }'
o=$(run "$D-pl1" "$MOD var p = { type:\"pseudo_module\", name:\"glue\", ops:[ $PG3 ] };
     var u = [p]; print(u::simplify(box(2,2,2), 1, 2, 3, 4));")
echo "$o" | grep -q "glue: takes 3" || { bad "札 'glue' がエラー文に出ない"; echo "$o" | grep -i error | head -1; }
o=$(run "$D-pl2" "$MOD var p = { type:\"pseudo_module\", name:\"cgal\", ops:[ $PG3 ] };
     var u = [p]; print(u::simplify(box(2,2,2), 1, 2, 3, 4));")
echo "$o" | grep -q "{pseudo}: takes 3" || { bad "実モジュールと同名の札が {pseudo} に倒れない"; echo "$o" | grep -i error | head -1; }
o=$(run "$D-pl3" "$MOD var p = { type:\"pseudo_module\", ops:[ $PG3 ] };
     var u = [p]; print(u::simplify(box(2,2,2), 1, 2, 3, 4));")
echo "$o" | grep -q "{pseudo}: takes 3" || { bad "name 省略が {pseudo} にならない"; echo "$o" | grep -i error | head -1; }
say "  9) 札がエラー文に出る / 省略と実モジュール同名は {pseudo} に倒す"

# ---- ⑪ vtail ----
#   ★ 可変部の種別。既定 "cache" なので、値を並べるなら "value" と書く必要がある。
PV='var p = { type:"pseudo_module",
             ops:[ { name:"simplify", in:["cache"], variadic:1, vtail:"value",
                     body:\(a){ 55; } } ] };'
V1=$(val "$D-pv1" "$MOD $PV var u=[p,\"cgal\"]; print(\"VAL\", u::simplify(box(2,2,2), 1, 2));")
[ "$V1" = "55" ] || bad "vtail:\"value\" で値の可変部が通らない '$V1'"
#   ★ 既定 (cache) なら値の可変部は当たらない ⇒ 降りて cgal が 3 引数を受けられずエラー。
#     ⚠ ここは **個数の列挙ではない** — 擬似は variadic なので「3 個」自体は受けられる
#     (合う候補が在るときは個数の話をしない、という規則どおり)。
PVD='var p = { type:"pseudo_module",
              ops:[ { name:"simplify", in:["cache"], variadic:1,
                      body:\(a){ 66; } } ] };'
o=$(run "$D-pv2" "$MOD $PVD var u=[p,\"cgal\"]; print(u::simplify(box(2,2,2), 1, 2));")
echo "$o" | grep -q "66" && bad "vtail の既定が \"cache\" になっていない (値を受けてしまった)"
echo "$o" | grep -qi 'error' || { bad "既定 vtail で降りた先がエラーにならない"; echo "$o" | head -2; }
say "  10) vtail は \"value\"/\"cache\" の文字列・既定 cache ($V1 / 既定では当たらず降りる)"

# ---- ⑫ どの候補も個数を受けられない ----
o=$(run "$D-pc1" "$MOD var p = { type:\"pseudo_module\", name:\"glue\", ops:[ $PG3 ] };
     var u = [p,\"cgal\"]; print(u::simplify(box(2,2,2), 1, 2, 3, 4));")
echo "$o" | grep -q "no candidate takes 5 argument(s)" || { bad "個数の違反を列挙していない"; echo "$o" | grep -i error | head -1; }
echo "$o" | grep -q "glue: takes 3" || bad "擬似の個数が列に出ない"
echo "$o" | grep -q "cgal: takes 2" || { bad "実モジュールの個数が列に出ない"; echo "$o" | grep -i error | head -1; }
# ★ 陰性対照: 個数が合う候補が 1 つでもあれば **この文言は出さない** (入力型の話になる)
o=$(run "$D-pc2" "$MOD
     var p = { type:\"pseudo_module\", name:\"glue\",
               ops:[ { name:\"simplify\", in:[\"cache\"], sig:[\"(oc-brep3d)->value\"], body:\(a){ 1; } } ] };
     var u = [p]; print(u::simplify(box(2,2,2)));")
echo "$o" | grep -q "no candidate takes" && { bad "個数が合うのに個数の話をしている"; echo "$o" | grep -i error | head -1; }
say "  11) 個数の違反を候補ごとに並べる (合う候補が在るときは言わない)"

# ---- ⑬ ★ #3570 段2: **引数の数が合わない実モジュールは候補から外れ、擬似へ降りる** ----
#   以前は候補列の中の実モジュールを **sig だけ**で見ていたので、その個数を受けられなくても
#   「実が居る」で通常 routing へ降り、擬似に届かないまま
#   @sphere: too many arguments (takes 2)@ になっていた。
#   ⚠ 通常 routing (候補列を書かない式) は **この段では変えない** — 下の陰性対照で見る。
PS3='var p = { type:"pseudo_module", name:"vox",
              ops:[ { name:"sphere", in:["value","value","value"], sig:["()->value"],
                      body:\(r,seg,dx){ 777; } } ] };'
S3=$(val "$D-a1" "$MOD $PS3 var u=[\"cgal\",p]; print(\"VAL\", u::sphere(1, 32, 5));")
[ "$S3" = "777" ] || bad "個数を受けられない実モジュールが候補から外れない '$S3'"
#   ★ 陰性対照 1: 個数が合えば **実モジュールが勝つ** (先頭だから)
S2=$(run "$D-a2" "$MOD $PS3 var u=[\"cgal\",p]; var m=u::sphere(1, 32); print(\"T\", type_of(m));")
echo "$S2" | grep -q "^T cg-mesh3d" || { bad "個数が合うのに実モジュールが勝っていない"; echo "$S2" | head -2; }
#   ★ 陰性対照 2: **候補列を書かない**同じ式は従来どおりのエラー (段2 は通常 routing を変えない)
S0=$(run "$D-a3" "$MOD print(sphere(1, 32, 5));")
#   ⚠ #3570 段4 で **通常 routing にも arity が入った** ので、ここの文言は
#     「どれも受けない」の列挙形になった。⇒ 陰性対照の意味は「擬似が答えていないこと」。
echo "$S0" | grep -q "no candidate takes 3 argument(s)" || { bad "通常 routing の文言が違う"; echo "$S0" | grep -i error | head -1; }
echo "$S0" | grep -q "777" && bad "候補列を書いていないのに擬似が答えている"
say "  12) 個数の合わない実モジュールは候補から外れ擬似へ降りる (擬似=$S3 ・ 通常 routing は不変)"

[ "$NG" -eq 0 ] && echo "MODULE-PSEUDO-OK"
exit "$NG"
