#!/bin/sh
# srava_ptsplit_openvdb.sh — 点群と距離場の **積と差** (#3580・openvdb.so)。
#
# ★★ 規約は geomutils 版 (test/srava_ptsplit.sh) と **同じ**。判定器だけ違う
#   (あちらは巻き数 / point-in-polygon ・ こちらは **距離場の符号**)。
#   ⇒ s[1] / s[2] はモジュールを跨いで一致するべき (#3581 の一致検定の材料)。
#
# ① 分割の不変条件   nverts(0) + nverts(-1) + nverts(+1) == nverts(A)
# ② difference       difference(A,M) ≡ intersection(A,M,+1)
# ③ ★★ **帯の外でも符号が生きている** — 球の中心 (境界から r 離れている = 狭帯域の外) が
#    「内側」と出ること。⚠ ここが落ちたら distance_at と同じく飽和で内外が言えなくなっている。
# ④ 2D 点群 = z=0 平面上の点として受ける (openvdb に 2D 型は無い)
# ⑤ 境界の帯 (0.75 ボクセル) に載る点が mode 0 に出る
# ⑥ 明示エラー       引数の順が逆 / mode が範囲外
#
# ⚠ 検定の点は **境界から dx 以上離して**置く (⑤ を除く)。境界ちょうどで組むと
#   dx を変えただけで落ちる脆い検定になる (#3580 の起票の注意)。
# ⚠ 成功行は失敗が無いときだけ出す (step)。値の比較で `set -- $(…)` を使わない。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
D="${SRAVA_CACHE_DIR:-/tmp/srava-ptsplit-vd}"
W="$D-work"
rm -rf "$W"; mkdir -p "$W" || exit 1
fails=0; _f0=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
step() { [ "$fails" = "$_f0" ] && echo "      $1"; _f0=$fails; }
cke()  { [ "$2" = "$3" ] || fail "$1: '$2' ≠ '$3'"; }

PRE='module("openvdb.so",{priority:99});
module("points.so",{});'

v() {
	{ echo "$PRE"; cat; } > "$W/$1.srv"
	out=$(SRAVA_CACHE_DIR="$W/c$1" "$SRAVA" "$W/$1.srv" 2>&1)
	case "$out" in
	*ERROR*|*error*) echo "PTSPLITVD-ERR[$1] $out" >&2; : > "$W-verror" ;;
	esac
	echo "$out" | sed -n 's/^V //p'
}
verr() {
	{ echo "$PRE"; cat; } > "$W/$1.srv"
	SRAVA_CACHE_DIR="$W/c$1" "$SRAVA" "$W/$1.srv" 2>&1 | grep -oE 'ERROR.*' | head -1
}
chk_err() { case "$2" in *"$3"*) ;; *) fail "$1: '$2' に '$3' が無い" ;; esac; }

# ---- ①②③⑤ 球 r=1 ・ dx=0.05 ⇒ 帯は 0.75*0.05 = 0.0375 ---------------------------
# 点: 原点 (境界から 1.0 = **帯の遥か外**) / 0.5 (内) / 2.0 (外) / 1.0 (境界ちょうど)
A=$(v a <<'EOF'
var g = sphere(1.0, 0.05);
var p = points3d([[0,0,0],[0.5,0,0],[2,0,0],[1,0,0]]);
print("V ", nverts(p), " ", nverts(intersection(p,g,-1)), " ",
            nverts(intersection(p,g,1)), " ", nverts(intersection(p,g,0)), " ",
            nverts(difference(p,g)));
EOF
)
echo "$A" | { read n in out bnd diff
  [ -n "$n" ] || fail "① 値が取れなかった"
  s=$(( in + out + bnd ))
  cke "① 分割の不変条件 (和 == 入力)" "$s"   "$n"
  cke "② difference == mode +1"       "$diff" "$out"
  cke "③ ★帯の外でも符号が生きる (内 2 点)" "$in"  "2"
  cke "⑤ 境界の帯に 1 点"              "$bnd"  "1"
}
step "1) 分割の不変条件 ・ difference ≡ mode +1 ・ 帯の外の符号 ・ 境界の帯"

# ---- ④ 2D 点群 = z=0 平面上の点 ------------------------------------------------------
B=$(v b <<'EOF'
var g = sphere(1.0, 0.05);
var q = points2d([[0,0],[0.5,0],[2,0],[1,0]]);
print("V ", nverts(intersection(q,g,-1)), " ", nverts(intersection(q,g,1)), " ",
            nverts(intersection(q,g,0)));
EOF
)
echo "$B" | { read i o bn
  cke "④ 2D 内側"  "$i"  "2"
  cke "④ 2D 外側"  "$o"  "1"
  cke "④ 2D 境界"  "$bn" "1"
  s=$(( i + o + bn )); cke "④ 2D 分割の不変条件" "$s" "4"
}
step "2) 2D 点群を z=0 平面上の点として受ける"

# ---- ⑥ 明示エラー --------------------------------------------------------------------
chk_err "⑥ 順が逆 (距離場, 点群)" \
  "$(verr e1 <<'EOF'
var p = points3d([[0,0,0]]);
print("V ", nverts(intersection(sphere(1.0,0.05), p, -1)));
EOF
)" "no module can execute op 'intersection'"
chk_err "⑥ mode が範囲外" \
  "$(verr e2 <<'EOF'
var p = points3d([[0,0,0]]);
print("V ", nverts(intersection(p, sphere(1.0,0.05), 7)));
EOF
)" "mode must be 0"
step "3) 明示エラー 2 件 (順が逆 / mode)"

[ -f "$W-verror" ] && { echo "FAIL: 走行中に ERROR が出ている (上の PTSPLITVD-ERR を見ること)"; fails=$((fails+1)); }

[ "$fails" = 0 ] || { echo "PTSPLITVD_FAIL: $fails 件"; exit 1; }
echo "PTSPLITVD-OK"
