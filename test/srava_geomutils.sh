#!/bin/sh
# ★★★ #3527 段 4: geomutils.so の **片の取り出し** (part / part_at / shell / shell_at) の検査。
#   $1 = srava / $2 = 入力を作るカーネル (manifold.so / geogram.so / cherchi.so)。
#
# ---- 何を見ているか ----
# ★ 段 4 の本丸は **入れ子** — 「どの空洞がどの塊のものか」。数えるだけ (nparts / nshells) なら
#   符号で足りるが、*取り出す* には入れ子が要る。そこが #3514 で part が見送られた分かれ目。
#   ⇒ 実体は meshprops.h の nesting() (3D・巻き数) と ringprops.h の nesting() (2D・点の内外)。
#
# ★★ **和の式が 2 つ在り、別物であること** がこの検査の柱:
#     part  … 値を *分割する* 片 ⇒ Σ |volume(part)|  == |volume(m)|  (**符号なし**)
#     shell … 面の連結成分      ⇒ Σ  volume(shell)  ==  volume(m)   (**符号つき**)
#   この 2 つが別であることが part と shell が別物である理由そのもの。
#
# ⚠⚠ 比較は **許容差つき**。gu は double なので中空の箱が 55.999999999999993 になる。
#   厳密一致で書くと *正しい実装が落ちる* (2026-09-17 の実測値)。
#
# ⚠⚠ **どちらが 0 番かは検査しない**。索引は走査順 = 実装依存で、番号は *指し示す* ためでは
#   なく **列挙のため** のものだから (#3527 ③)。⇒ 見るのは「集合として正しいか」と「和」。
#
# ⚠⚠ 成功行は **その節で 1 件も失敗していないときだけ** 出す (step)。無条件に echo すると
#   *落ちているのに緑の行が並ぶ* — 装置が嘘をつく形になる (2026-09-17 に一度そう書いた)。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
SO="${2:-manifold.so}"
D="${SRAVA_CACHE_DIR:-/tmp/srava-geomutils}"
W="$D-work"
rm -rf "$W"; mkdir -p "$W" || exit 1
fails=0; _f0=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
step() { [ "$fails" = "$_f0" ] && echo "      $1"; _f0=$fails; }
# 相対許容差つきの一致 (1e-9)。$1=得た値 $2=期待値
near() { awk -v a="$1" -v b="$2" 'BEGIN{d=a-b; if(d<0)d=-d; s=(b<0?-b:b); if(s<1)s=1; exit !(d<=1e-9*s)}'; }
ckn()  { near "$2" "$3" || fail "$1: '$2' ≠ $3"; }

# ★ 源は **ファイルに書く**。SRAVA_SOURCE への文字列合成は引用が絡んで壊れやすく、
#   壊れると「式が走らなかった」のに検査だけが落ちる = 原因が読めない形になる。
#   ⇒ @SO@ だけを差し替える。
mksrc() {   # $1 = 出力名 ・ 標準入力 = 源
	sed "s/@SO@/$SO/" > "$W/$1.srv"
}
run() {     # $1 = 源の名前 ・ $2 = キャッシュ dir の枝
	# ★ @OUT@ を持つ源は枝ごとに別ファイルへ書く (⑩ が 2 走の入力を突き合わせるため)。
	sed "s|@OUT@|$W/c$2.off|" "$W/$1.srv" > "$W/run$2.srv"
	SRAVA_CACHE_DIR="$W/c$2" "$SRAVA" "$W/run$2.srv" 2>&1
}

# 中空の箱: 4^3 の中に 2^3 の空洞 ⇒ 体積 56 ・ 殻 2 枚 (+64 と -8) ・ 塊 1 個
# ★ 入れ子の本番: 10^3 の中の 6^3 の空洞の中に 2^3 の島
#   ⇒ 殻 3 枚 (+1000 / -216 / +8) ・ **塊 2 個** (箱+空洞 = 784 と 島 = 8) ・ 体積 792
#   ★★ 島が **別の塊**になることが「直接の空洞だけを抱える」の検定。外殻が島まで
#     抱え込んでいたら塊は 1 個になり、part(m,0) が 792 になってしまう。

mksrc count <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
print("NS", nshells(hollow)); print("NP", nparts(hollow)); print("V", volume(hollow));
EOF
mksrc shells <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
var i = 0;
for ( i = 0 ; i < nshells(hollow) ; i = i + 1 ) { print("SV", volume(shell(hollow,i))); }
EOF
mksrc parts <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
var i = 0;
for ( i = 0 ; i < nparts(hollow) ; i = i + 1 ) { print("PV", volume(part(hollow,i))); }
EOF
mksrc nest <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var nest = union(difference(box(10,10,10), translate(box(6,6,6),[2,2,2])), translate(box(2,2,2),[4,4,4]));
print("NS", nshells(nest)); print("NP", nparts(nest)); print("V", volume(nest));
var i = 0;
for ( i = 0 ; i < nparts(nest) ; i = i + 1 ) { print("PV", volume(part(nest,i))); }
EOF
mksrc at <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var nest = union(difference(box(10,10,10), translate(box(6,6,6),[2,2,2])), translate(box(2,2,2),[4,4,4]));
print("MAT", volume(part_at(nest,[1,1,1])));
print("ISL", volume(part_at(nest,[5,5,5])));
print("COR", volume(shell_at(nest,[0,0,0])));
print("SIS", volume(shell_at(nest,[5,5,5])));
EOF
mksrc at_cav <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var nest = union(difference(box(10,10,10), translate(box(6,6,6),[2,2,2])), translate(box(2,2,2),[4,4,4]));
print("X", volume(part_at(nest,[3,3,3])));
EOF
mksrc at_out <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var nest = union(difference(box(10,10,10), translate(box(6,6,6),[2,2,2])), translate(box(2,2,2),[4,4,4]));
print("X", volume(part_at(nest,[99,99,99])));
EOF
mksrc tie <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
print("X", volume(shell_at(hollow,[2,2,0.5])));
EOF
mksrc two_d <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var r2 = union(difference(rect(10,10), translate(rect(6,6),[2,2])), translate(rect(2,2),[4,4]));
print("A", area(r2)); print("NP", nparts(r2));
var i = 0;
for ( i = 0 ; i < nparts(r2) ; i = i + 1 ) { print("PA", area(part(r2,i))); }
print("MAT", area(part_at(r2,[1,1])));
print("ISL", area(part_at(r2,[5,5])));
EOF
mksrc face3d <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
var sec = section(hollow, [0,0,2], [0,0,1], 0);
print("A", area(sec)); print("NP", nparts(sec));
print("P0", area(part(sec,0)));
print("MAT", area(part_at(sec,[0.5,0.5,2])));
EOF
mksrc face3d_off <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
var sec = section(hollow, [0,0,2], [0,0,1], 0);
print("X", area(part_at(sec,[0.5,0.5,0])));
EOF
mksrc face3d_hole <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
var sec = section(hollow, [0,0,2], [0,0,1], 0);
print("X", area(part_at(sec,[2,2,2])));
EOF
mksrc two_d_hole <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var r2 = difference(rect(4,4), translate(rect(2,2),[1,1]));
print("X", area(part_at(r2,[2,2])));
EOF
mksrc oob <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
print("X", volume(shell(hollow,99)));
EOF
mksrc shell2d <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
print("X", area(shell(rect(2,3), 0)));
EOF
mksrc rettype <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));
export("@OUT@", part(hollow,0));
EOF
mksrc reidx <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var nest = union(difference(box(10,10,10), translate(box(6,6,6),[2,2,2])), translate(box(2,2,2),[4,4,4]));
var i = 0;
for ( i = 0 ; i < nshells(nest) ; i = i + 1 ) { print("SV", volume(shell(nest,i))); }
export("@OUT@", nest);
EOF

# ---- ① 前提 (数えるほう) ----------------------------------------------------------
O=$(run count 1)
NS=$(printf '%s\n' "$O" | sed -n 's/^NS //p'); NP=$(printf '%s\n' "$O" | sed -n 's/^NP //p')
V=$(printf  '%s\n' "$O" | sed -n 's/^V //p')
[ "$NS" = "2" ] || fail "中空の箱の nshells が 2 でない: '$NS'"
[ "$NP" = "1" ] || fail "中空の箱の nparts が 1 でない: '$NP'"
ckn "中空の箱の volume" "$V" 56
step "① 中空の箱: nshells=$NS ・ nparts=$NP ・ volume=$V"

# ---- ② shell: 集合として {+64,-8} ・ **Σ 符号つき** == 全体 ------------------------
SV=$(run shells 2 | sed -n 's/^SV //p')
SVS=$(printf '%s\n' "$SV" | sort -n | tr '\n' ' ')
case "$SVS" in "-8 64 ") : ;; *) fail "殻の体積が集合として {+64,-8} でない: [$SVS]" ;; esac
SSUM=$(printf '%s\n' "$SV" | awk '{s+=$1} END{printf "%.15g", s}')
ckn "Σ 符号つき体積 == 全体" "$SSUM" "$V"
step "② shell: {+64,-8} ・ Σ **符号つき** = $SSUM == volume(全体)"

# ---- ③ part: **Σ 符号なし** == 全体 ・ 殻だけ返していないこと ----------------------
PV=$(run parts 3 | sed -n 's/^PV //p')
PSUM=$(printf '%s\n' "$PV" | awk '{s+=($1<0?-$1:$1)} END{printf "%.15g", s}')
ckn "Σ |volume(part)| == 全体" "$PSUM" "$V"
# ★ 塊 1 個の形では part(m,0) は **m と同じもの** — 空洞を落として 64 を返してはいけない。
#   ⚠ ここが入れ子を解けていないときに真っ先に壊れる箇所 (陽性対照)。
ckn "part(m,0) が元の値と同じでない (殻だけ返していないか)" "$(printf '%s\n' "$PV" | head -1)" "$V"
step "③ part: Σ **符号なし** = $PSUM == volume(全体) ・ part(m,0) は空洞を抱えている"

# ---- ④ ★★ 入れ子の本番 — 空洞の中の島は **別の塊** ------------------------------
O=$(run nest 4)
NS4=$(printf '%s\n' "$O" | sed -n 's/^NS //p'); NP4=$(printf '%s\n' "$O" | sed -n 's/^NP //p')
V4=$(printf  '%s\n' "$O" | sed -n 's/^V //p'); PV4=$(printf '%s\n' "$O" | sed -n 's/^PV //p')
[ "$NS4" = "3" ] || fail "入れ子の nshells が 3 でない: '$NS4'"
[ "$NP4" = "2" ] || fail "入れ子の nparts が 2 でない: '$NP4' (島を別の塊と見ていない)"
ckn "入れ子の volume" "$V4" 792
PSUM4=$(printf '%s\n' "$PV4" | awk '{s+=($1<0?-$1:$1)} END{printf "%.15g", s}')
ckn "入れ子 Σ |volume(part)| == 全体" "$PSUM4" "$V4"
PVS4=$(printf '%s\n' "$PV4" | sort -n | awk '{printf "%.15g ", $1}')
case "$PVS4" in "8 784 ") : ;;
  *) fail "入れ子の塊の体積が {784,8} でない: [$PVS4] (外殻が島まで抱えていないか)" ;;
esac
step "④ ★ 入れ子: 殻 3 枚 ・ 塊 2 個 {784, 8} ・ Σ |volume| = $PSUM4 == $V4"

# ---- ⑤ part_at — 点を **含む** 塊 (いちばん近い塊ではない) -------------------------
# ★★ shell_at が「いちばん近い殻」なのにこちらが「含む」なのは、**立体は内側を持ち
#   曲面は持たない**から。⇒ **空洞の中には塊が無い** と正しく答えられるのが要点で、
#   最近傍で代用するとここが嘘になる。
O=$(run at 5)
ckn "part_at 材料の中 -> 塊 784" "$(printf '%s\n' "$O" | sed -n 's/^MAT //p')" 784
ckn "part_at 島の中   -> 塊 8"   "$(printf '%s\n' "$O" | sed -n 's/^ISL //p')" 8
run at_cav 6 | grep -q 'no solid of this mesh is at that point' \
  || fail "part_at が **空洞の中**で断らなかった (そこに材料は無い)"
run at_out 7 | grep -q 'no solid of this mesh is at that point' \
  || fail "part_at が **立体の外**で断らなかった"
step "⑤ part_at: 材料=784 ・ 島=8 ・ **空洞の中と外は断る** (最近傍で代用しない)"

# ---- ⑥ shell_at — 位置で指す ・ 同距離は断る -------------------------------------
ckn "shell_at 角 -> 外殻 +1000" "$(printf '%s\n' "$O" | sed -n 's/^COR //p')" 1000
ckn "shell_at 島の中心 -> 島 +8" "$(printf '%s\n' "$O" | sed -n 's/^SIS //p')" 8
run tie 8 | grep -q 'equally near' \
  || fail "同距離の点で shell_at が断らなかった (黙って片方を選んでいる)"
step "⑥ shell_at: 位置で指す ・ 同距離は **断る**"

# ---- ⑦ 2D — 3D と **同じ相似形** ----------------------------------------------------
# ★ 2D の塊 = 符号つき面積が正のリング。穴の中の島は 3D と同じく **別の塊**。
#
# ⚠⚠ **「型が無い」と「本当の歯抜け」を混ぜない** (#3527 / op_parity の教訓)。geogram と
#   cherchi は 3D 専用で @rect@ すら持たない ⇒ この節は *対象外* であって歯抜けではない。
#   ★ 判定は **カーネル名の表**で行う。probe (rect が作れるか試す) にすると、manifold が
#     2D を失っても「対象外」として静かに飛ばしてしまう — 回帰が緑で通る形になる。
#   ⚠ 2D を持つカーネルが増えたらこの表に足すこと。
case "$SO" in
  manifold.so|cgal.so) HAS2D=1 ;;
  *)                   HAS2D=0 ;;
esac
if [ "$HAS2D" = "0" ]; then
step "⑦ 2D: $SO は 2D 型を持たない (3D 専用) ので **対象外** — 歯抜けではない"
else
O=$(run two_d 9)
A7=$(printf '%s\n' "$O" | sed -n 's/^A //p'); NP7=$(printf '%s\n' "$O" | sed -n 's/^NP //p')
PA7=$(printf '%s\n' "$O" | sed -n 's/^PA //p')
[ "$NP7" = "2" ] || fail "2D の nparts が 2 でない: '$NP7' (島を別の塊と見ていない)"
ckn "2D の area" "$A7" 68
ASUM=$(printf '%s\n' "$PA7" | awk '{s+=($1<0?-$1:$1)} END{printf "%.15g", s}')
ckn "2D Σ |area(part)| == 全体" "$ASUM" "$A7"
ckn "2D part_at 材料 -> 64" "$(printf '%s\n' "$O" | sed -n 's/^MAT //p')" 64
ckn "2D part_at 島   -> 4"  "$(printf '%s\n' "$O" | sed -n 's/^ISL //p')" 4
run two_d_hole 10 | grep -q 'no piece of this 2D region is at that point' \
  || fail "2D の part_at が **穴の中**で断らなかった"
step "⑦ 2D: 塊 2 個 ・ Σ |area| = $ASUM == $A7 ・ 穴の中は断る (3D と同じ相似形)"

# ---- ⑦b **face3d** (置き場所を持つ 2D) — 点は *world* で与える ---------------------
# ★ #3533 の 2 型を両方通す。cross2d は枠が既定なので to_local が「z が 0 か」に縮退し、
#   **枠の効いている道を一度も通らない** ⇒ 断面 (原点が z=2 の枠) で別に見る。
# ⚠⚠ 面外の点は **断る** — 黙って射影すると「平面の外の点で訊いたのに答えが返る」= 嘘になる。
O=$(run face3d 16)
A8=$(printf '%s\n' "$O" | sed -n 's/^A //p'); NP8=$(printf '%s\n' "$O" | sed -n 's/^NP //p')
[ "$NP8" = "1" ] || fail "face3d の nparts が 1 でない: '$NP8'"
ckn "face3d の area (4x4 から 2x2 を抜いた枠)" "$A8" 12
ckn "face3d part(sec,0) == 全体 (穴を抱えている)" "$(printf '%s\n' "$O" | sed -n 's/^P0 //p')" "$A8"
ckn "face3d part_at (平面上の world 点)" "$(printf '%s\n' "$O" | sed -n 's/^MAT //p')" "$A8"
run face3d_off 17 | grep -q 'not on the plane of this 2D region' \
  || fail "face3d の part_at が **平面の外の点**で断らなかった (黙って射影していないか)"
run face3d_hole 18 | grep -q 'no piece of this 2D region is at that point' \
  || fail "face3d の part_at が穴の中で断らなかった"
step "⑦b face3d: world の点で指す ・ **面外は断る** (射影しない) ・ 穴の中も断る"
fi

# ---- ⑧ 断り方 — 範囲外 / 2D に shell ----------------------------------------------
run oob 11 | grep -q 'out of range' || fail "範囲外の索引がエラーにならなかった"
# ★ 2D に殻は無い。sig が 3D の行しか持たないので **ルータが先に弾く**。
#   ⚠ op 側のメッセージを期待すると *通らない道* を検査していることになる (srava_shell.sh の教訓)。
#   ⚠ 2D 型を持たないカーネルでは rect そのものが作れないので、この行は 2D 側だけで見る。
if [ "$HAS2D" = "1" ]; then
	run shell2d 12 | grep -q "no module can execute op 'shell'" \
	  || fail "2D に shell を当ててもエラーにならなかった"
	step "⑧ 断り方: 範囲外 ・ 2D の shell は sig が弾く"
else
	step "⑧ 断り方: 範囲外 (2D の節は対象外)"
fi

# ---- ⑨ ★★ **答えたのが geomutils であること** の確認 (routing の陽性対照) ----------
# ⚠⚠ これが無いと「別のカーネルが同じ値を返しているだけ」を見分けられない。#3527 段 3 で
#   陽性対照が *何も検定していなかった* のがまさにこの形 (cgal が priority で取っていた)。
# ★ part の返り型は **gu-mesh3d**。export はまだこの型を受けないので、型名を名指しで断る。
#   ⇒ そのエラー文言そのものが「gu が答えた」の証拠になる。
#   ⚠ export が gu を受けるようになったらこの検査は空振りする ⇒ そのときは別の証拠に替えること。
run rettype 13 | grep -q "gu-mesh3d" \
  || fail "part の返り型が gu-mesh3d でない (別のモジュールが答えている)"
step "⑨ ★ routing: part の返りは **gu-mesh3d** (geomutils が答えている)"

# ---- ⑩ 索引の再現性 — ★ **材料を先に較正する** ------------------------------------
# ⚠⚠ 2026-09-17 の実測: この式を manifold で組むと、**入力メッシュそのものが走ごとに
#   2 通りのバイト列になる** (op 間並列が被演算子の構築順を入れ替え、Manifold の内部 id 順が
#   変わるため。SRAVA_LOAD_AGENT=1 で直列にすると 20/20 でもう一方に固定される)。
#   ⇒ 殻の並びが変わったとき、それを gu の非決定性と読むと **誤診**になる。
#   ★ だから先に「入力が同じバイト列か」を測り、*同じだったときだけ* 並びを問う。
#   ⚠ 入力が違ったときも **集合**は必ず一致していなければならない (gu の不変量)。
R1=$(run reidx 14); R2=$(run reidx 15)
S1=$(printf '%s\n' "$R1" | sed -n 's/^SV //p'); S2=$(printf '%s\n' "$R2" | sed -n 's/^SV //p')
[ "$(printf '%s\n' "$S1" | sort -n | tr '\n' ' ')" = "$(printf '%s\n' "$S2" | sort -n | tr '\n' ' ')" ] \
  || fail "2 回の実行で殻の **集合** が違う: [$S1] 対 [$S2]"
H1=$(md5sum < "$W/c14.off" 2>/dev/null | cut -d' ' -f1)
H2=$(md5sum < "$W/c15.off" 2>/dev/null | cut -d' ' -f1)
if [ -n "$H1" ] && [ "$H1" = "$H2" ]; then
	[ "$S1" = "$S2" ] || fail "**同じ入力**なのに同じ i が別の殻を指した (gu の索引が決定的でない)"
	step "⑩ 索引の再現性: 入力が同一バイト列 ⇒ 並びも同一"
else
	step "⑩ 索引: 入力メッシュ自体が走ごとに違う ($SO の上流が非決定的) ⇒ 並びは問わない・集合は一致"
fi

# ==================================================================
# ★★ #3553: **点との距離** (distance_at)。定義は 3D / 2D で 1 つ —
#   「p から **面の集合** までの最短距離 (符号なし)」。
#   ⇒ これで mf / ch は **初めて** distance_at を持つ (cg / gg / oc / vd は自前を持っていた)。
# ⚠⚠ 柱は「平面へ射影して 2D で測る」ことを **していない** ことの確認 —
#   面の真上の点は 0 ではなく高さそのものが出なければならない (射影案だと 0 になる)。
#   ⇒ #3533 / #3534 で何度も直した *置き場所が黙って落ちる* 事故と同じ形を作らない。
# ==================================================================
mksrc dist <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var b = box(2,2,2);
print("D", distance_at(b,[1,1,5]));
print("D", distance_at(b,[1,1,1]));
print("D", distance_at(b,[-3,1,1]));
EOF
# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
_SPLIT_=$(run dist 30 | sed -n 's/^D //p')
set -f
set -- $_SPLIT_
set +f
# 箱 (0..2)³ の外の点 z=5 ⇒ 上面まで 3
ckn "箱の外の点から面まで" "$1" 3
# ★ 中心から面まで **1** (符号なし = 内側でも正)。⇒ 0 を返す実装だと落ちる
ckn "★ 中心から面まで (符号なし)" "$2" 1
ckn "x 側の外から面まで" "$3" 3
step "⑨ 点との距離: 面の集合までの最短距離 (内側でも正)"

if [ "$HAS2D" = "1" ]; then
mksrc dist2d <<'EOF'
module("@SO@",{priority:99});
module("geomutils.so",{});
var r = rect(3,2);
print("D", distance_at(r,[1.5,1,0]));
print("D", distance_at(r,[1.5,1,0.7]));
print("D", distance_at(r,[5,4,0]));
print("D", distance_at(rotate(r,"x",90),[1.5,0,1]));
print("D", distance_at(rotate(r,"x",90),[1.5,0.5,1]));
EOF
_SPLIT_=$(run dist2d 31 | sed -n 's/^D //p')
set -f
set -- $_SPLIT_
set +f
ckn "2D の上の点は 0"                  "$1" 0
# ★★ ここが射影案との分かれ目 — 射影すると 0 になる
ckn "★ 真上 0.7 は 0.7 (射影しない)"    "$2" 0.7
ckn "角の外は 2√2"                     "$3" 2.8284271247461903
# ★ 枠を持つ 2D (face3d) — 面の中の点なので 0
ckn "傾いた 2D の面内の点は 0"          "$4" 0
# ★ 枠の法線方向に 0.5 離れた点 ⇒ 0.5 (枠を無視すると 0 か別の値になる)
ckn "★ 傾いた 2D の法線方向 0.5"        "$5" 0.5
step "⑩ 2D の距離: 枠 (平面) を効かせて 3D 距離を答える"
fi

if [ "$fails" != "0" ]; then
	echo "FAIL: $fails 件"
	exit 1
fi
echo "GEOMUTILS_OK 入れ子を解いて塊を取り出す ・ part は符号なし / shell は符号つきで和が全体に一致"
