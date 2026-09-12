#!/bin/sh
# occt の text / extrude / revolve / prism (#3471)。$1 = srava 実行体。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR, SRAVA_PATH。
#
# ★ TrueType の字形を **2D の曲線 (Bezier / B-spline) のまま** 取り込み、平面上の Face
#   (oc-cross2d) にする。押し出すと側面は平面の帯ではなく **厳密な押し出し面**になる。
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
NG=0
MOD='include "module/all.sra";'

# ★ フォントが無い機械では **スキップする** (テストの前提であって検証対象ではない)。
FONT=""
for f in /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
         /usr/share/fonts/dejavu/DejaVuSans.ttf \
         /usr/share/fonts/TTF/DejaVuSans.ttf \
         /Library/Fonts/Arial.ttf ; do
	[ -f "$f" ] && { FONT="$f"; break; }
done
FONT2=""
for f in /usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf \
         /usr/share/fonts/dejavu/DejaVuSerif.ttf ; do
	[ -f "$f" ] && { FONT2="$f"; break; }
done

val() { rm -rf "$D"; SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$MOD $1" "$SRAVA" 2>&1 | sed -n 's/^VAL //p' | tr -d '"'; }
msg() { rm -rf "$D"; SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$MOD $1" "$SRAVA" 2>&1; }
near() { awk -v a="$1" -v b="$2" -v t="$3" 'BEGIN{ if(b==0){print (a==0)?0:1; exit} d=(a-b)/b; if(d<0)d=-d; print (d<=t)?0:1 }'; }
ck() { if [ -z "$2" ]; then echo "TEXT_FAIL: $1 が値を出さない"; NG=1; return; fi
       if [ "$(near "$2" "$3" "$4")" = "0" ]; then echo "  ok $1 ($2)"
       else echo "TEXT_FAIL: $1 = $2 / 期待 $3 (許容 $4)"; NG=1; fi }

# ---- ① prism は **メッシュ系と厳密に一致**する (平面 n+2 枚なので。box と同じ理由) ----
OP=$(val 'print("VAL", volume("occt"::prism(6,2,1)));')
CP=$(val 'print("VAL", volume("cgal"::prism(6,2,1)));')
if [ -n "$OP" ] && [ -n "$CP" ] && [ "$(near "$OP" "$CP" 1e-12)" = "0" ]; then
	echo "  ok prism は cgal と一致 ($OP)"
else echo "TEXT_FAIL: prism が cgal と一致しない (occt=$OP cgal=$CP)"; NG=1; fi

if [ -z "$FONT" ]; then
	echo "  (フォントが見つからないので text 系はスキップ)"
	[ "$NG" -eq 0 ] && echo "OCCT-TEXT-OK"
	exit "$NG"
fi

# ---- ② text は oc-cross2d を作る ----
case "$(msg "print(\"occt\"::text(\"$FONT\", \"O\", 10));")" in
	*oc-cross2d*) echo "  ok text は oc-cross2d を作る" ;;
	*) echo "TEXT_FAIL: text が oc-cross2d を作らない"; NG=1 ;;
esac

# ---- ③ ★ 穴が引かれている & extrude が area と厳密に整合する ----
#      'O' は外周と内周を持つ。extrude(h) の体積は area*h でなければならない。
A=$(val "print(\"VAL\", area(\"occt\"::text(\"$FONT\", \"O\", 10)));")
V=$(val "print(\"VAL\", volume(extrude(\"occt\"::text(\"$FONT\", \"O\", 10), 5)));")
if [ -n "$A" ] && [ -n "$V" ]; then
	EXP=$(awk -v a="$A" 'BEGIN{ printf "%.17g", a*5 }')
	ck "extrude の体積 = area x 高さ" "$V" "$EXP" 1e-12
else echo "TEXT_FAIL: area / extrude が値を出さない"; NG=1; fi

# ---- ④ ★ 曲線が保たれている (多角形化されていない) ----
#      'O' の輪郭は Bezier の弧の並び。多角形化されていれば面数は桁違いに増える。
NF=$(val "print(\"VAL\", nfaces(extrude(\"occt\"::text(\"$FONT\", \"O\", 10), 5)));")
if [ -n "$NF" ] && [ "$NF" -le 40 ]; then echo "  ok 曲線が保たれている (nfaces=$NF・多角形化なら桁違いに増える)"
else echo "TEXT_FAIL: nfaces=$NF が多すぎる (輪郭が多角形化されている疑い)"; NG=1; fi

# ---- ⑤ ★ フォントは D_REF: 別のフォントなら別の形になる ----
if [ -n "$FONT2" ]; then
	A2=$(val "print(\"VAL\", area(\"occt\"::text(\"$FONT2\", \"O\", 10)));")
	if [ -n "$A2" ] && [ "$(near "$A" "$A2" 1e-6)" != "0" ]; then
		echo "  ok フォントを変えると形が変わる ($A -> $A2)"
	else echo "TEXT_FAIL: フォントを変えても形が変わらない ($A / $A2)"; NG=1; fi
fi

# ---- ⑥ revolve が回転体を作る ----
RV=$(val "print(\"VAL\", volume(revolve(\"occt\"::text(\"$FONT\", \"I\", 10), 360)));")
if [ -n "$RV" ]; then echo "  ok revolve は回転体を作る ($RV)"
else echo "TEXT_FAIL: revolve が値を出さない"; NG=1; fi

# ---- ⑦ フォント名 (パスでない) は明示エラー。★ 再現性のため名前引きは受けない ----
case "$(msg "print(area(\"occt\"::text(\"DejaVu Sans\", \"O\", 10)));")" in
	*"cannot open the font file"*) echo "  ok フォント名は受けない (パス必須)" ;;
	*) echo "TEXT_FAIL: フォント名が明示エラーにならない"; NG=1 ;;
esac

# ---- ⑧ ★ #3472: polygonize — 曲線の輪郭を折れ線へ落とす (cast ではない・粒度が要る) ----
#      defl を細かくすると occt の厳密面積へ収束すること = 落とし方が正しいことの示し方。
P1=$(val "print(\"VAL\", area(polygonize(\"occt\"::text(\"$FONT\", \"O\", 10), 0.2)));")
P2=$(val "print(\"VAL\", area(polygonize(\"occt\"::text(\"$FONT\", \"O\", 10), 0.002)));")
if [ -n "$P1" ] && [ -n "$P2" ] && [ -n "$A" ]; then
	R=$(awk -v a="$P1" -v b="$P2" -v x="$A" \
	    'BEGIN{ da=(x-a); db=(x-b); if(da<0)da=-da; if(db<0)db=-db; print (db < da/5)?0:1 }')
	if [ "$R" = "0" ]; then echo "  ok polygonize は defl を細かくすると厳密面積へ収束 ($P1 -> $P2 / 厳密 $A)"
	else echo "TEXT_FAIL: polygonize の収束が見えない ($P1 -> $P2 / 厳密 $A)"; NG=1; fi
else echo "TEXT_FAIL: polygonize が値を出さない"; NG=1; fi

# ---- ⑨ ★ 落とした先で既存の 2D 資産が使えること ----
#      mf-cross2d → extrude / cast で cgal 2D へ昇格 → cgal の extrude が一致すること。
T="polygonize(\"occt\"::text(\"$FONT\", \"O\", 10), 0.01)"
VM=$(val "print(\"VAL\", volume(extrude($T, 5)));")
VC=$(val "print(\"VAL\", volume(extrude(cast(\"cg-cross2d\", $T), 5)));")
if [ -n "$VM" ] && [ -n "$VC" ] && [ "$(near "$VM" "$VC" 1e-12)" = "0" ]; then
	echo "  ok 落とした先で 2D 資産が使える (mf $VM / cgal 経由 $VC)"
else echo "TEXT_FAIL: mf-cross2d と cast 後の cgal で結果が違う ($VM / $VC)"; NG=1; fi

# ---- ⑩ ★ 粒度は必須 (省略や 0 以下は明示エラー) ----
case "$(msg "print(area(polygonize(\"occt\"::text(\"$FONT\", \"O\", 10), 0)));")" in
	*"must be > 0"*) echo "  ok polygonize は粒度必須" ;;
	*) echo "TEXT_FAIL: polygonize の粒度 0 が明示エラーにならない"; NG=1 ;;
esac

[ "$NG" -eq 0 ] && echo "OCCT-TEXT-OK"
exit "$NG"
