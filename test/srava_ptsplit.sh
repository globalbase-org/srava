#!/bin/sh
# srava_ptsplit.sh — 点群と mesh の **積と差** (#3579・geomutils.so)。
#
# ★★★ 検定の本体は **境界ちょうどの点が第 3 の集合に出ること**。
#   「境界は含む」と決め打った実装でも ①②④ は通ってしまうので、③ の不変条件と
#   ⑤ の境界そのものを見る。⇒ 決め打ちに戻したら **和が合わなくなって落ちる**。
#
# ① 分割の不変条件   nverts(0) + nverts(-1) + nverts(+1) == nverts(A)
# ② difference       difference(A,M) ≡ intersection(A,M,+1) (同じ数・同じ点)
# ③ 空洞             空洞の中の点は **外** (最近傍で代用すると内と答えてしまう)
# ④ 2D               穴あき領域でも 3D と **同じ答え方** (穴の中は外)
# ⑤ 境界             面の上に載せた点が mode 0 に出る (整数格子では普通に起きる)
# ⑥ 明示エラー       X>Y / 順が逆 / mode が範囲外
#
# ⚠⚠ 成功行は **その節で 1 件も失敗していないときだけ** 出す (step)。
# ⚠ 値の比較で `set -- $(…)` を使わないこと — `[1,1,0]` が文字クラスに食われる。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
SO="${2:-manifold.so}"
D="${SRAVA_CACHE_DIR:-/tmp/srava-ptsplit}"
W="$D-work"
rm -rf "$W"; mkdir -p "$W" || exit 1
fails=0; _f0=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
step() { [ "$fails" = "$_f0" ] && echo "      $1"; _f0=$fails; }
cke()  { [ "$2" = "$3" ] || fail "$1: '$2' ≠ '$3'"; }

# ★ 源は **ファイルに書く** (文字列合成は引用で壊れる)。@SO@ だけ差し替える。
PRE='module("@SO@",{priority:99});
module("geomutils.so",{});
module("points.so",{});'

# $1 = 枝の名前 ・ 標準入力 = 源の本体。★ v() は **エラー行を見たら番を残す** —
#   空文字どうしの比較で「通ってしまう」形を塞ぐ (#3572 で実際に踏んだ)。
v() {
	{ echo "$PRE"; cat; } | sed "s/@SO@/$SO/" > "$W/$1.srv"
	out=$(SRAVA_CACHE_DIR="$W/c$1" "$SRAVA" "$W/$1.srv" 2>&1)
	case "$out" in
	*ERROR*|*error*) echo "PTSPLIT-ERR[$1] $out" >&2; : > "$W-verror" ;;
	esac
	echo "$out" | sed -n 's/^V //p'
}
# エラーを **期待する**とき (v と違い ERROR で番を残さない)。
verr() {
	{ echo "$PRE"; cat; } | sed "s/@SO@/$SO/" > "$W/$1.srv"
	SRAVA_CACHE_DIR="$W/c$1" "$SRAVA" "$W/$1.srv" 2>&1 | grep -oE 'ERROR.*' | head -1
}
chk_err() {   # $1 = 名前 ・ $2 = 得た行 ・ $3 = 含むべき文字列
	case "$2" in *"$3"*) ;; *) fail "$1: '$2' に '$3' が無い" ;; esac
}

# ---- ①②⑤ 箱と一様格子の点群 ------------------------------------------------------
# ★ rand の整数格子は **境界に載る点が普通に出る** ⇒ ここが本番 (#3575 の 97% の実測)。
A=$(v a <<'EOF'
var b = box(2,2,2);
var p = rand([-1,-1,-1],[3,3,3], 200, 7);
print("V ", nverts(p), " ", nverts(intersection(p,b,0)), " ",
            nverts(intersection(p,b,-1)), " ", nverts(intersection(p,b,1)), " ",
            nverts(difference(p,b)));
EOF
)
# ⚠ glob に食わせないため read で割る (set -- $(…) は使わない)。
echo "$A" | { read n bnd in out diff
  [ -n "$n" ] || fail "① 値が取れなかった"
  s=$(( bnd + in + out ))
  cke "① 分割の不変条件 (和 == 入力)" "$s" "$n"
  cke "② difference == mode +1"       "$diff" "$out"
  [ "${bnd:-0}" -gt 0 ] || fail "⑤ 境界の点が 1 つも出ない (整数格子なら必ず載る)"
  echo "$n $bnd $in $out" > "$W-a"
}
step "1) 分割の不変条件 ・ difference ≡ mode +1 ・ 境界が空でない"

# ---- ③ 空洞の中は **外** ------------------------------------------------------------
# 10^3 の中に [4,6]^3 の空洞。[5,5,5]=空洞の中 / [1,5,5]=材料 / [4,5,5]=空洞の壁 / [20,5,5]=遠い外
B=$(v b <<'EOF'
var h = difference(box(10,10,10), translate(box(2,2,2),[4,4,4]));
var p = points3d([[5,5,5],[1,5,5],[4,5,5],[20,5,5]]);
print("V ", nverts(intersection(p,h,1)), " ", nverts(intersection(p,h,-1)), " ",
            nverts(intersection(p,h,0)));
EOF
)
echo "$B" | { read o i bn
  cke "③ 空洞の中 + 遠い外 = 外 2 点" "$o"  "2"
  cke "③ 材料の中 = 1 点"             "$i"  "1"
  cke "③ 空洞の壁 = 境界 1 点"        "$bn" "1"
}
step "2) 空洞の中は外 (最近傍で代用すると内と答える所)"

# ---- ④ 2D も **同じ答え方** ----------------------------------------------------------
C=$(v c <<'EOF'
var s = difference(rect(10,10), translate(rect(2,2),[4,4]));
var p = points2d([[1,1],[5,5],[0,5],[20,5]]);
print("V ", nverts(intersection(p,s,-1)), " ", nverts(intersection(p,s,1)), " ",
            nverts(intersection(p,s,0)), " ", nverts(difference(p,s)));
EOF
)
echo "$C" | { read i o bn df
  cke "④ 2D 材料の中"        "$i"  "1"
  cke "④ 2D 穴の中 + 遠い外" "$o"  "2"
  cke "④ 2D 境界"            "$bn" "1"
  cke "④ 2D difference"      "$df" "$o"
  s=$(( i + o + bn )); cke "④ 2D 分割の不変条件" "$s" "4"
}
step "3) 2D も 3D と同じ答え方 (穴の中は外)"

# ---- ⑥ 明示エラー --------------------------------------------------------------------
chk_err "⑥ X>Y (3D 点群 x 2D 領域)" \
  "$(verr e1 <<'EOF'
var p = points3d([[1,1,1]]);
print("V ", nverts(intersection(p, rect(4,4), -1)));
EOF
)" "no module can execute op 'intersection'"
chk_err "⑥ 順が逆 (mesh, 点群)" \
  "$(verr e2 <<'EOF'
var p = points3d([[1,1,1]]);
print("V ", nverts(intersection(box(2,2,2), p, -1)));
EOF
)" "no module can execute op 'intersection'"
chk_err "⑥ mode が範囲外" \
  "$(verr e3 <<'EOF'
var p = points3d([[1,1,1]]);
print("V ", nverts(intersection(p, box(2,2,2), 7)));
EOF
)" "mode must be 0"
step "4) 明示エラー 3 件 (X>Y / 順が逆 / mode)"

# ⚠ v() が拾ったエラーは **ここで赤にする** ($( ) の中では exit が効かない)。
[ -f "$W-verror" ] && { echo "FAIL: 走行中に ERROR が出ている (上の PTSPLIT-ERR を見ること)"; fails=$((fails+1)); }

[ "$fails" = 0 ] || { echo "PTSPLIT_FAIL: $fails 件"; exit 1; }
echo "PTSPLIT-OK"
