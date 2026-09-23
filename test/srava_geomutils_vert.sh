#!/bin/sh
# ★★ #3527 段 5: geomutils.so の **頂点を読む** op (vert / verts / face_verts) の検査。
#   $1 = srava / $2 = 入力を作るカーネル (manifold.so / geogram.so / cherchi.so)。
#
# ---- 何を見ているか ----
# ★ #3527 の「やること 4」= @nverts@ で数えられるのに **座標を読む op が 1 本も無かった** 穴。
#   3 つ組のうち「取り出す」(vert) と「まとめて」(verts)、それに面の **頂点番号** (face_verts)。
#
# ★★ 柱は 3 つ:
#     ① verts(m) の i 番目 == vert(m,i)          ← 2 つが **同じ列を同じ順**で歩いていること
#     ② Σ area(face_verts が指す三角形) == area(m) ← 番号が **本当にその面を指している**こと
#     ③ face3d は **world の 3 成分**             ← 置き場所が落ちていないこと
#   ⚠ ①は op_vert と op_verts を別々に書くと黙ってずれる。②が無いと「番号が返る」だけで
#     *中身が合っているか* を見ていない (返り値の形だけを見る検査になる)。
#
# ⚠⚠ 比較は **許容差つき**。gu は double。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
SO="${2:-manifold.so}"
D="${SRAVA_CACHE_DIR:-/tmp/srava-gu-vert}"
W="$D-work"
rm -rf "$W"; mkdir -p "$W" || exit 1
fails=0; _f0=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
step() { [ "$fails" = "$_f0" ] && echo "      $1"; _f0=$fails; }
near() { awk -v a="$1" -v b="$2" 'BEGIN{d=a-b; if(d<0)d=-d; s=(b<0?-b:b); if(s<1)s=1; exit !(d<=1e-9*s)}'; }
ckn()  { near "$2" "$3" || fail "$1: '$2' ≠ $3"; }

mksrc() { sed "s/@SO@/$SO/" > "$W/$1.srv"; }
run()   { SRAVA_CACHE_DIR="$W/c$2" "$SRAVA" "$W/$1.srv" 2>&1; }

# ★ 2D 型を持つのは manifold だけ (geogram / cherchi は 3D 専用 = **対象外**であって歯抜けではない)。
#   ⚠ probe でなく **カーネル名の表**で判断する — probe だと manifold が 2D を失っても静かに飛ぶ。
case "$SO" in
  manifold.so|cgal.so) HAS2D=1 ;;
  *)                   HAS2D=0 ;;
esac

mksrc basic <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
module("points.so",{});
var b = box(2,3,4);
print("NV", nverts(b)); print("NF", nfaces(b)); print("A", area(b));
var i = 0;
for ( i = 0 ; i < nverts(b) ; i = i + 1 ) { var v = vert(b,i); print("P", v[0], v[1], v[2]); }
EOF
mksrc same <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
module("points.so",{});
var b = box(2,3,4);
var c = verts(b);
print("NC", nverts(c));
var i = 0; var bad = 0;
for ( i = 0 ; i < nverts(b) ; i = i + 1 ) {
  var a = vert(b,i);
  var d = vert(c,i);
  if ( a[0] != d[0] ) { bad = bad + 1; }
  if ( a[1] != d[1] ) { bad = bad + 1; }
  if ( a[2] != d[2] ) { bad = bad + 1; }
}
print("BAD", bad);
EOF
# ★★ face_verts が指す三角形から **面積と符号つき体積を自分で組み立てて** m と突き合わせる。
# ⚠⚠ この 2 つは **面の並べ替えに対して不変**なので、「i 番目が本当に i 番目の面か」は
#   *見ていない*。実測で確認した (2026-09-17: face_verts を 1 つ隣の面へずらしても両方通った)。
#   ⇒ 見ているのは「**面の集合が m の面と一致すること**」と「**向きが揃っていること**」。
#   ★ 並べ替えを検出する手立ては無い — **面 i を名指す口が face_verts の他に無い**ので、
#     「i 番目の面」は face_verts 自身が定義している。索引が実装依存である以上それでよく、
#     代わりに **同じ i が同じ面を指し続けること**を⑧で別に見る (#3527 ③ の検証条件)。
#   ⚠ 面積だけだと **向きの反転を見逃す** (面積は向きに依らない) ので符号つき体積を足してある。
mksrc faces <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
module("points.so",{});
var b = box(2,3,4);
var i = 0; var s = 0; var vol = 0; var oob = 0;
for ( i = 0 ; i < nfaces(b) ; i = i + 1 ) {
  var f = face_verts(b,i);
  if ( f[0] < 0 ) { oob = oob + 1; }
  if ( f[0] >= nverts(b) ) { oob = oob + 1; }
  var p = vert(b,f[0]);
  var q = vert(b,f[1]);
  var r = vert(b,f[2]);
  var ux = q[0]-p[0]; var uy = q[1]-p[1]; var uz = q[2]-p[2];
  var vx = r[0]-p[0]; var vy = r[1]-p[1]; var vz = r[2]-p[2];
  var cx = uy*vz - uz*vy; var cy = uz*vx - ux*vz; var cz = ux*vy - uy*vx;
  s = s + 0.5 * sqrt(cx*cx + cy*cy + cz*cz);
  vol = vol + (cx*p[0] + cy*p[1] + cz*p[2]) / 6;
}
print("OOB", oob); print("SUM", s); print("VOL", vol);
EOF
mksrc faces_idx <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var b = box(2,3,4);
var i = 0;
for ( i = 0 ; i < nfaces(b) ; i = i + 1 ) { var f = face_verts(b,i); print("FV", f[0], f[1], f[2]); }
EOF
mksrc oob <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var b = box(2,3,4);
print("X", vert(b, 99)[0]);
EOF
mksrc noload <<'EOF'
module("@SO@",{priority:99});
var b = box(2,3,4);
print("X", vert(b, 0)[0]);
EOF
mksrc twod <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
module("points.so",{});
var r = rect(2,3);
print("N2", nverts(r)); print("NC2", nverts(verts(r)));
var i = 0;
for ( i = 0 ; i < nverts(r) ; i = i + 1 ) { var v = vert(r,i); print("Q", v[0], v[1]); }
EOF
mksrc twod_face <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
print("X", face_verts(rect(2,3), 0)[0]);
EOF
mksrc face3d <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
module("points.so",{});
var b = box(4,4,10);
var s2 = section(b, [0,0,2], [0,0,1], 0);
var s7 = section(b, [0,0,7], [0,0,1], 0);
print("NF3", nverts(s7)); print("NC3", nverts(verts(s7)));
var i = 0;
for ( i = 0 ; i < nverts(s7) ; i = i + 1 ) { var v = vert(s7,i); print("F", v[0], v[1], v[2]); }
for ( i = 0 ; i < nverts(s2) ; i = i + 1 ) { var v = vert(s2,i); print("G", v[0], v[1], v[2]); }
EOF

# ---- ① 3D の基本 ---------------------------------------------------------------
O=$(run basic 1)
NV=$(printf '%s\n' "$O" | sed -n 's/^NV //p'); NF=$(printf '%s\n' "$O" | sed -n 's/^NF //p')
A=$(printf  '%s\n' "$O" | sed -n 's/^A //p');  P=$(printf '%s\n' "$O" | sed -n 's/^P //p')
[ "$NV" = "8" ]  || fail "box の nverts が 8 でない: '$NV'"
[ "$NF" = "12" ] || fail "box の nfaces が 12 でない: '$NF'"
NP=$(printf '%s\n' "$P" | grep -c .)
[ "$NP" = "8" ] || fail "vert が 8 点返さなかった: '$NP'"
# ★ 相異なる 8 点で、和は (8, 12, 16) = 各軸 4 点ずつが 0 と辺長
[ "$(printf '%s\n' "$P" | sort -u | grep -c .)" = "8" ] || fail "vert が同じ点を 2 度返した"
SUM=$(printf '%s\n' "$P" | awk '{x+=$1;y+=$2;z+=$3} END{printf "%g %g %g", x,y,z}')
[ "$SUM" = "8 12 16" ] || fail "box(2,3,4) の頂点の和が (8 12 16) でない: ($SUM)"
step "① 3D: nverts=$NV ・ nfaces=$NF ・ 相異なる 8 点 ・ 和 ($SUM)"

# ---- ② ★★ verts(m) の i 番目 == vert(m,i) ---------------------------------------
# ⚠⚠ **ここが一番壊れやすい**。op_vert と op_verts は別々に列を歩くので、片方だけ直すと黙ってずれる。
O=$(run same 2)
NC=$(printf '%s\n' "$O" | sed -n 's/^NC //p'); BAD=$(printf '%s\n' "$O" | sed -n 's/^BAD //p')
[ "$NC" = "8" ]  || fail "verts(box) の点数が 8 でない: '$NC'"
[ "$BAD" = "0" ] || fail "verts(m) の i 番目が vert(m,i) と違う点が $BAD 成分あった"
step "② verts(m)[i] == vert(m,i) が全 8 点で成立 (点群の点数 $NC)"

# ---- ③ ★★ face_verts が指す三角形が **m の面そのもの** か -------------------------
# ⚠ 「番号が範囲内」だけでは *どの頂点を指していても* 通る ⇒ 面積と符号つき体積を自分で組む。
# ⚠⚠ どちらも **面の並べ替えには不変** なので「i 番目が本当に i 番目か」は見ていない
#   (実測済: 1 つ隣の面へずらしても両方通る)。並べ替えの検出は⑧の再現性が受け持つ。
O=$(run faces 3)
OOB=$(printf '%s\n' "$O" | sed -n 's/^OOB //p'); FSUM=$(printf '%s\n' "$O" | sed -n 's/^SUM //p')
FVOL=$(printf '%s\n' "$O" | sed -n 's/^VOL //p')
[ "$OOB" = "0" ] || fail "face_verts が範囲外の頂点番号を返した ($OOB 件)"
ckn "Σ area(face_verts の三角形) == area(m)" "$FSUM" "$A"
# ★ 符号つき体積は **向きに効く** — 面積だけだと巻き方の反転を見逃す。box(2,3,4) は 24。
ckn "Σ 符号つき体積(face_verts の三角形) == volume(m)" "$FVOL" 24
step "③ face_verts: 範囲内 ・ Σ 面積 = $FSUM == area(m) $A ・ Σ 符号つき体積 = $FVOL (向きも揃う)"

# ---- ④ 断り方 --------------------------------------------------------------------
run oob 4 | grep -q 'out of range' || fail "範囲外の索引がエラーにならなかった"
if [ "$HAS2D" = "1" ]; then
	# ★ 2D は面を持たない。sig が 3D の行しか持たないので **ルータが先に弾く**。
	run twod_face 5 | grep -q "no module can execute op 'face_verts'" \
	  || fail "2D に face_verts を当ててもエラーにならなかった"
	step "④ 断り方: 範囲外 ・ 2D の face_verts は sig が弾く"
else
	step "④ 断り方: 範囲外 (2D の節は対象外)"
fi

# ---- ⑤ ★★ 答えたのが geomutils であること (陽性対照) ------------------------------
# ⚠⚠ これが無いと「別のカーネルが同じ値を返しているだけ」を見分けられない (#3527 段 3 の③)。
# ★ geomutils.so を **ロードしない**と op そのものが無くなる ⇒ そのエラーが「gu が答えていた」証拠。
#   ⚠ 文言は 2 通りある。**どの op 名も登録されていない**ときはパーサ段階で止まるので
#     "undefined variable: vert" になり、型だけ合わないときは "no module can execute op" になる。
#     ⇒ 片方だけを期待すると *通らない道* を見ることになる (実際 1 度そう書いて落ちた)。
run noload 6 | grep -qE "undefined variable: vert|no module can execute op 'vert'" \
  || fail "geomutils を外しても vert が通った (別のモジュールが答えている)"
step "⑤ ★ 陽性対照: geomutils を外すと vert が消える (gu が答えている)"

# ---- ⑥ 2D — cross2d は **2 成分** -------------------------------------------------
if [ "$HAS2D" = "0" ]; then
	step "⑥ 2D: $SO は 2D 型を持たない (3D 専用) ので **対象外** — 歯抜けではない"
else
O=$(run twod 7)
N2=$(printf '%s\n' "$O" | sed -n 's/^N2 //p'); NC2=$(printf '%s\n' "$O" | sed -n 's/^NC2 //p')
Q=$(printf '%s\n' "$O" | sed -n 's/^Q //p')
[ "$N2" = "4" ]  || fail "rect(2,3) の nverts が 4 でない: '$N2'"
[ "$NC2" = "4" ] || fail "verts(rect) の点数が 4 でない: '$NC2'"
SUM2=$(printf '%s\n' "$Q" | awk '{x+=$1;y+=$2} END{printf "%g %g", x,y}')
[ "$SUM2" = "4 6" ] || fail "rect(2,3) の頂点の和が (4 6) でない: ($SUM2)"
step "⑥ 2D (cross2d): **2 成分** ・ nverts=$N2 ・ 和 ($SUM2)"

# ---- ⑦ ★★ face3d は **world の 3 成分** -------------------------------------------
# ★★ #3533 が bbox / centroid で決めた「face3d は world」へ揃えた (ひさ判断 2026-09-17)。
#   ⚠⚠ 枠の中の 2 成分だと *置き場所が黙って落ちる* — **違う高さの断面が同じ答えを返す**。
#     ⇒ ここでは「3 成分になった」だけでなく **2 枚が別の答えであること**まで見る。
#       前者だけだと *中身が枠内のまま* でも通ってしまう。
O=$(run face3d 8)
NF3=$(printf '%s\n' "$O" | sed -n 's/^NF3 //p'); NC3=$(printf '%s\n' "$O" | sed -n 's/^NC3 //p')
F=$(printf '%s\n' "$O" | sed -n 's/^F //p'); G=$(printf '%s\n' "$O" | sed -n 's/^G //p')
[ -n "$NF3" ] && [ "$NF3" = "$NC3" ] \
  || fail "face3d の nverts と verts の点数が違う: '$NF3' 対 '$NC3'"
# ★ z=7 の断面なので **全点の z が 7**
ZBAD=$(printf '%s\n' "$F" | awk '$3 != 7 {n++} END{print n+0}')
[ "$ZBAD" = "0" ] || fail "face3d の vert の z が 7 でない点が $ZBAD 個 (world になっていない)"
[ "$(printf '%s\n' "$F" | awk 'NF==3' | grep -c .)" = "$NF3" ] \
  || fail "face3d の vert が 3 成分で返っていない"
[ "$F" = "$G" ] && fail "z=2 と z=7 の断面が同じ vert を返した (置き場所が落ちている)"
step "⑦ face3d: **world の 3 成分** (全点 z=7) ・ 別の高さは別の答え"
fi

# ---- ⑧ 索引の再現性 — **同じ i が同じ面を指し続けるか** ----------------------------
# ★ face_verts の索引は実装依存 (#3527 ③)。③ が並べ替えを検出できない以上、
#   「同じ式・同じ i が同じ面を指す」はここでしか見られない。
# ⚠ 入力が走ごとに変わるカーネル (manifold) では並びも変わりうる ⇒ #3549 と同じく
#   **集合の一致は常に**・**並びは入力が同じときだけ**問う、とすべきだが、box は leaf 1 つで
#   ブールを通らないため入力は決定的。⇒ ここは並びをそのまま問う。
R1=$(run faces_idx 9 | sed -n 's/^FV //p')
R2=$(run faces_idx 10 | sed -n 's/^FV //p')
[ -n "$R1" ] || fail "face_verts の列が取れなかった"
[ "$R1" = "$R2" ] || fail "同じ式・同じ i が別の面を指した (索引が決定的でない)"
step "⑧ 索引の再現性: 別キャッシュで計算し直しても face_verts の並びが同一"

if [ "$fails" != "0" ]; then
	echo "FAIL: $fails 件"
	exit 1
fi
echo "GEOMUTILS_VERT_OK vert / verts / face_verts は同じ列を同じ順で見ている ・ face3d は world"
