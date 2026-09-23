#!/bin/sh
# srava_ptsplit_occt.sh — 点群と B-rep の **積と差** (#3581・occt.so) と **3 モジュール一致**。
#
# ★★ 規約は geomutils 版 / openvdb 版と **同じ**。判定器だけ違う
#   (BRepClass3d_SolidClassifier ・ 解析曲面のまま解く = 3 つの中で一番正確)。
#
# ① 分割の不変条件   nverts(0) + nverts(-1) + nverts(+1) == nverts(A)
# ② difference       difference(A,M) ≡ intersection(A,M,+1)
# ③ ★ **解析球の半径ちょうど**が境界に出る ← occt でしか書けない検定
#    (メッシュ系は内接多角形なので「半径ちょうど」が構造的に境界にならない)
# ④ 2D (oc-cross2d)
# ⑤ ★★★ **3 モジュール一致** — 同じ箱を occt / geomutils / openvdb で作り、同じ点群の
#    内側 / 外側の個数が **一致**すること。⚠ 境界の厚みはモジュールごとに違うので、
#    比べるのは s[1] / s[2] だけ・点は **境界から十分離して**置く。
# ⑥ 明示エラー       X>Y / 引数の順が逆 / mode が範囲外
#
# ⚠ 成功行は失敗が無いときだけ出す (step)。値の比較で `set -- $(…)` を使わない。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
D="${SRAVA_CACHE_DIR:-/tmp/srava-ptsplit-oc}"
W="$D-work"
rm -rf "$W"; mkdir -p "$W" || exit 1
fails=0; _f0=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
step() { [ "$fails" = "$_f0" ] && echo "      $1"; _f0=$fails; }
cke()  { [ "$2" = "$3" ] || fail "$1: '$2' ≠ '$3'"; }

PRE='module("occt.so",{priority:99});
module("points.so",{});'

v() {
	{ echo "$PRE"; cat; } > "$W/$1.srv"
	out=$(SRAVA_CACHE_DIR="$W/c$1" "$SRAVA" "$W/$1.srv" 2>&1)
	case "$out" in
	*ERROR*|*error*) echo "PTSPLITOC-ERR[$1] $out" >&2; : > "$W-verror" ;;
	esac
	echo "$out" | sed -n 's/^V //p'
}
verr() {
	{ echo "$PRE"; cat; } > "$W/$1.srv"
	SRAVA_CACHE_DIR="$W/c$1" "$SRAVA" "$W/$1.srv" 2>&1 | grep -oE 'ERROR.*' | head -1
}
chk_err() { case "$2" in *"$3"*) ;; *) fail "$1: '$2' に '$3' が無い" ;; esac; }

# ---- ①②③ 解析球 r=1 -----------------------------------------------------------------
A=$(v a <<'EOF'
var s = sphere(1.0);
var p = points3d([[0,0,0],[0.5,0,0],[2,0,0],[1,0,0]]);
print("V ", nverts(p), " ", nverts(intersection(p,s,-1)), " ",
            nverts(intersection(p,s,1)), " ", nverts(intersection(p,s,0)), " ",
            nverts(difference(p,s)));
EOF
)
echo "$A" | { read n in out bnd diff
  [ -n "$n" ] || fail "① 値が取れなかった"
  s=$(( in + out + bnd ))
  cke "① 分割の不変条件 (和 == 入力)"     "$s"   "$n"
  cke "② difference == mode +1"           "$diff" "$out"
  cke "③ ★解析球の半径ちょうど = 境界 1 点" "$bnd" "1"
  cke "③ 内側 2 点"                        "$in"  "2"
}
step "1) 分割の不変条件 ・ difference ≡ mode +1 ・ 解析球の半径ちょうど"

# ---- ④ 2D (oc-cross2d) ---------------------------------------------------------------
B=$(v b <<'EOF'
var r = rect(10,10);
var q = points2d([[1,1],[5,5],[20,5],[0,5]]);
print("V ", nverts(intersection(q,r,-1)), " ", nverts(intersection(q,r,1)), " ",
            nverts(intersection(q,r,0)));
EOF
)
echo "$B" | { read i o bn
  cke "④ 2D 内側" "$i" "2"
  cke "④ 2D 外側" "$o" "1"
  cke "④ 2D 境界" "$bn" "1"
  s=$(( i + o + bn )); cke "④ 2D 分割の不変条件" "$s" "4"
}
step "2) 2D (oc-cross2d)"

# ---- ⑤ ★★★ 3 モジュール一致 ----------------------------------------------------------
# ⚠ 点は **境界から十分離して**置く。境界の厚みはモジュールごとに違う (occt=Confusion /
#   geomutils=相対 1e-12 / openvdb=0.75 ボクセル) ので、境界付近で比べると *正しくても* 割れる。
{ cat <<'EOF'
module("occt.so",{priority:99});
module("manifold.so",{priority:50});
module("openvdb.so",{priority:40});
module("openvdb_mf.so",{});
module("geomutils.so",{});
module("points.so",{});
var p = points3d([[1,1,1],[2,2,2],[3.5,3.5,3.5],[-1,2,2],[2,-1,2],[9,9,9],[2,2,5]]);
var bo = "occt"::box(4,4,4);
var bm = "manifold"::box(4,4,4);
var bg = cast("gu-mesh3d", bm);
var bv = voxelize(bm, 0.05);
print("V ", nverts(intersection(p,bo,-1)), " ", nverts(intersection(p,bo,1)), " ",
            nverts(intersection(p,bg,-1)), " ", nverts(intersection(p,bg,1)), " ",
            nverts(intersection(p,bv,-1)), " ", nverts(intersection(p,bv,1)));
EOF
} > "$W/agree.srv"
AG=$(SRAVA_CACHE_DIR="$W/cagree" "$SRAVA" "$W/agree.srv" 2>&1)
case "$AG" in *ERROR*) echo "PTSPLITOC-ERR[agree] $AG" >&2; : > "$W-verror" ;; esac
echo "$AG" | sed -n 's/^V //p' | { read oi oo gi go vi vo
  [ -n "$oi" ] || fail "⑤ 値が取れなかった"
  # ★ 陽性対照: そもそも内も外も 0 でないこと (0 と 0 を比べて「一致」にしない)
  [ "${oi:-0}" -gt 0 ] && [ "${oo:-0}" -gt 0 ] || fail "⑤ 内/外が空 — 比較になっていない"
  cke "⑤ occt == geomutils (内側)"  "$oi" "$gi"
  cke "⑤ occt == geomutils (外側)"  "$oo" "$go"
  cke "⑤ occt == openvdb  (内側)"   "$oi" "$vi"
  cke "⑤ occt == openvdb  (外側)"   "$oo" "$vo"
}
step "3) ★3 モジュール一致 (occt / geomutils / openvdb で内側・外側が同数)"

# ---- ⑥ 明示エラー --------------------------------------------------------------------
chk_err "⑥ X>Y (3D 点群 x 2D 領域)" \
  "$(verr e1 <<'EOF'
var p = points3d([[1,1,1]]);
print("V ", nverts(intersection(p, rect(4,4), -1)));
EOF
)" "no module can execute op 'intersection'"
chk_err "⑥ 順が逆 (形, 点群)" \
  "$(verr e2 <<'EOF'
var p = points3d([[1,1,1]]);
print("V ", nverts(intersection(sphere(1.0), p, -1)));
EOF
)" "no module can execute op 'intersection'"
chk_err "⑥ mode が範囲外" \
  "$(verr e3 <<'EOF'
var p = points3d([[1,1,1]]);
print("V ", nverts(intersection(p, sphere(1.0), 7)));
EOF
)" "mode must be 0"
step "4) 明示エラー 3 件 (X>Y / 順が逆 / mode)"

[ -f "$W-verror" ] && { echo "FAIL: 走行中に ERROR が出ている (上の PTSPLITOC-ERR を見ること)"; fails=$((fails+1)); }

[ "$fails" = 0 ] || { echo "PTSPLITOC_FAIL: $fails 件"; exit 1; }
echo "PTSPLITOC-OK"
