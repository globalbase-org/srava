#!/bin/sh
# ★★ #3554 段1: **`op#変種` の行**が routing に効いていることの回帰。
#
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR, SRAVA_MODULE_PATH。
#
# ---- なぜ demo モジュールで見るのか ----
# ★ 段1 は「まだどの製品 op も変種行を持たない」ので、**製品の op だけを見ていると
#   “機能不変” しか確かめられず、前方一致が本当に効いているか誰も暴けない**。
#   ⇒ demo.so に検定専用の変種行を 3 本置いて、*実際に走らせて*固定する。
#   ⚠ これは #3511 の "[]" 書式が「入れ子を渡す式」を持たなかったために 525788d まで
#     再入不可を隠していたのと同じ形の対策 (ひさ 2026-09-18)。
#
# ---- 何を固定するか ----
#   ① 前方一致    基底名 demo_pick で引くと demo_pick#a / #b が候補に入る
#   ② 先勝ち      OPS に先に書いた #a が (成立するなら) 選ばれる
#   ②' ★ #3554 段2: **1 行目が外れたら 2 行目**。demo_pick#a は *第 1 引数が 1 のときだけ*
#       成立するマッチ関数 (AK_MATCH) を持つので、demo_pick(9) は #b が勝つ
#   ②''★ 値が **上流 op の結果**でも待って読む (compact) ・ cold/warm で行が割れない
#   ③ 基底行なし  demo_only は **変種行しか無い** が引ける (規則③)
#   ④ 行名が C_OP に載る
#      ★★ ④ は別の検定を書かなくても ①〜③ が通ることで示される — demo_compute は
#        **op 名で分岐**しているので、行名が載っていなければ agent の lookup_op が
#        "unknown op: demo_pick" で落ちる。**黙って基底行に落ちることはできない**。
#   ⑤ 既存の基底行 (demo_add) が従来どおり
#
# ---- ✔ 段1 では検定できていなかったこと (段2 で埋まった) ----
# 「**1 行目が成立せず 2 行目が選ばれる**」形は、段1 では書けない。段1 の選び分けは sig だけで、
# 値 op の "->value" は **常に成立する**ので必ず 1 行目で決まるため。
#   ⇒ 較正で **op_row を 1 行目しか返さないようにしても、この検定は緑のまま**だった
#     (= 候補行を 2 行目以降まで見ていることを、ここでは暴けない)。
#   ★ 段2 (AK_MATCH) で「#a のマッチ関数が偽 → #b が勝つ」を書いたときに初めて効く。
#     ⇒ **段2 (AK_MATCH) で ②' として足した**。demo_pick(9) が 2 を返すことが、
#       候補行を 2 行目まで見ていることの証拠になる。
#
# ★ 較正で赤くなることは確かめてある (2026-09-19):
#     routedOpName を無効化              → 赤 (行名が C_OP に載らない)
#     supports_op を完全一致に戻す       → 赤 (パース時に「未定義の変数」になる)
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
. "$(dirname "$0")/srava_hangwatch.sh"

rm -rf "$D"
OUT=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE='module("demo.so",{priority:99});
	print("PICK", demo_pick(1));
	print("MISS", demo_pick(9));
	print("WAIT", demo_pick(demo_add(0,1)));
	print("ONLY", demo_only());
	print("ADD",  demo_add(2,3));' "$SRAVA" 2>&1)

get() { echo "$OUT" | sed -n "s/^$1 //p"; }
P=$(get PICK); M=$(get MISS); W=$(get WAIT); O=$(get ONLY); A=$(get ADD)

[ "$P" = "1" ] || { echo "FAIL: demo_pick(1) が $P (期待 1 = #a のマッチが真)"; echo "$OUT"; exit 1; }
# ★★ #3554 段2 の核心: **1 行目が外れて 2 行目が選ばれる**。
#   ⚠ 段1 ではこの形が書けず (sig だけの選び分けでは 1 行目が常に成立する)、候補行の走査は
#     **未検定のまま残っていた**。較正でも op_row を 1 行目だけにして緑だった。ここで埋まる。
[ "$M" = "2" ] || { echo "FAIL: demo_pick(9) が $M (期待 2 = #a が外れて #b が勝つ)"; echo "$OUT"; exit 1; }
# ★★ 値が **上流 op の結果**でも、待って読んで正しい行へ行く (compact して待つ)。
#   ⚠ 「読めたときだけ見る」設計だとここが cold/warm で割れる (2026-08-19 の 4CC と同型)。
[ "$W" = "1" ] || { echo "FAIL: demo_pick(demo_add(0,1)) が $W (期待 1 = 値を待って読む)"; echo "$OUT"; exit 1; }
[ "$O" = "2" ] || { echo "FAIL: demo_only が $O (期待 2 = 基底行が無くても変種行で引ける)"; echo "$OUT"; exit 1; }
[ "$A" = "5" ] || { echo "FAIL: demo_add が $A (期待 5 = 既存の基底行が壊れた)"; echo "$OUT"; exit 1; }

# ⑨ ★★ #3554 段3: **共通のマッチ述語** (pig_val_keeps_xy → srava_affine::keeps_z_plane) が
#    routing に効いていること。行列が z=0 平面を平面へ写すなら #flat (1)・面外なら #any (2)。
#    ⚠⚠ この述語は **書かれてから一度も呼ばれていなかった** (2026-09-19 に grep して 0 件)。
#      段4 で transform#xy に使う前に、ここで固定する。
#    ★★ 肝は **閾値の両側**を押さえること (affine.h の実測コメントを検定に落としたもの):
#      ・rotate("x",180) の m21 = sin(π) = 1.2246e-16 は **正当な平面→平面の変換** (2D では
#        mirror("y") と同じ) なので **1** でなければならない。厳密な 0 と比べる実装だと 2 になる
#      ・意図した面外回転は 1e-9 度でも m21 ≈ 1.7e-11 なので **2** になる = 閾値で分離できる
rm -rf "$D-x"
X=$(SRAVA_CACHE_DIR="$D-x" SRAVA_SOURCE='module("demo.so",{priority:99});
	print("X", demo_xy([1,0,0,0, 0,1,0,0, 0,0,1,0]));
	print("X", demo_xy([0,-1,0,0, 1,0,0,0, 0,0,1,0]));
	print("X", demo_xy([2,0,0,5, 0,2,0,7, 0,0,2,0]));
	print("X", demo_xy([1,0,0,0, 0,1,0,0, 0,0,-1,0]));
	print("X", demo_xy([1,0,0,0, 0,-1,-1.2246467991473532e-16,0, 0,1.2246467991473532e-16,-1,0]));
	print("X", demo_xy([1,0,0,0, 0,0,-1,0, 0,1,0,0]));
	print("X", demo_xy([1,0,0,0, 0,1,0,0, 0,0,1,3]));
	print("X", demo_xy([1,0,0,0, 0,0.9999999999999999,-1.7453292519943296e-11,0,
	                    0,1.7453292519943296e-11,0.9999999999999999,0]));
	print("X", demo_xy([1,0,0,0, 0,0,0,0, 0,0,1,0]));' "$SRAVA" 2>&1 | sed -n 's/^X //p' | tr '\n' ' ')
#        恒等 Rz90 S+T Mz sin(π)   | Rx90 Tz 1e-9度 特異
EXP="1 1 1 1 1 2 2 2 2 "
[ "$X" = "$EXP" ] || { echo "FAIL: 平面判定が [$X] (期待 [$EXP])"; exit 1; }
rm -rf "$D-x"

# ⑧ **cold でも warm でも同じ行へ行く**。同じキャッシュ dir で 2 回流して値が変わらないこと。
#    ⚠ routing が「値を読めたときだけ見る」形だと、1 回目 (cold) と 2 回目 (warm) で行が割れる。
rm -rf "$D-w"
for i in 1 2; do
	W2=$(SRAVA_CACHE_DIR="$D-w" SRAVA_SOURCE='module("demo.so",{priority:99});
		print("W", demo_pick(demo_add(0,1)));' "$SRAVA" 2>&1 | sed -n 's/^W //p')
	[ "$W2" = "1" ] || { echo "FAIL: $i 回目の demo_pick(上流の値) が $W2 (期待 1)"; exit 1; }
done
rm -rf "$D-w"

# ⑦ **`module::op` の指名からも変種が引ける** (指名で候補を絞ってから前方一致)。
#    ⚠ docs/sig_grammar_design.md §4.5 に書いた主張なので、**文書だけにして検定を置かない**と
#      前提が外れたときに気づけない。
rm -rf "$D-q"
Q=$(SRAVA_CACHE_DIR="$D-q" SRAVA_SOURCE='module("demo.so",{priority:99});
	print("Q", "demo"::demo_pick(1)); print("Q", "demo"::demo_only());' "$SRAVA" 2>&1 |
	sed -n 's/^Q //p' | tr '\n' ' ')
[ "$Q" = "1 2 " ] || { echo "FAIL: 指名経由の変種が [$Q] (期待 [1 2 ])"; exit 1; }
rm -rf "$D-q"

# ⑥ **キャッシュキーが行ごとに分かれる**: op 名は結果ハッシュの先頭に混ざる (compute_arg_hash)。
#    ⇒ 同じ引数でも別の行なら別の実体になる。⚠ ここが崩れると「変種で出力型が変わるのに
#      キーが同じ」= 前の行の答えを次の行が拾う、という最も見つけにくい壊れ方になる。
rm -rf "$D-k"
K=$(SRAVA_CACHE_DIR="$D-k" SRAVA_SOURCE='module("demo.so",{priority:99});
	print("V", demo_pick(1)); print("V", demo_only());' "$SRAVA" 2>&1 |
	sed -n 's/.*cache: \([0-9]*\) hit(s), \([0-9]*\) miss(es).*/\2/p')
[ "$K" = "2" ] || { echo "FAIL: 2 つの行で miss が $K (期待 2 = 行ごとに別のキャッシュ実体)"; exit 1; }

rm -rf "$D" "$D-k"
echo "OP-VARIANT-OK 前方一致/先勝ち/基底行なし/行名が C_OP に載る/キーが分かれる"
