#!/bin/sh
# occt の tube (#3470)。$1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR, SRAVA_PATH。
#
# ★★ occt の tube は **他カーネルの tube とは形が違う**:
#     cgal / manifold   折れ線の背骨 + segs 角形近似の断面 (共通 src/h/common/tube.h)
#     occt              点を通る C2 B-spline の背骨 + **厳密な円**の断面
#   ⇒ 厳密に一致させることはできないので kernel_agree には入れない。
#     代わりに **閉形式との一致**で検証する (カーネル一致より強い)。
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
NG=0
MOD='include "module/all.sra";'

val() { rm -rf "$D"; SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$MOD $1" "$SRAVA" 2>&1 |
        sed -n 's/^VAL //p'; }
msg() { rm -rf "$D"; SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="$MOD $1" "$SRAVA" 2>&1; }
# near <実測> <期待> <許容相対誤差> → 0/1
near() { awk -v a="$1" -v b="$2" -v t="$3" 'BEGIN{ if(b==0){print (a==0)?0:1; exit} d=(a-b)/b; if(d<0)d=-d; print (d<=t)?0:1 }'; }
ck() { # ck <ラベル> <実測> <期待> <許容>
	if [ -z "$2" ]; then echo "TUBE_FAIL: $1 が値を出さない"; NG=1; return; fi
	if [ "$(near "$2" "$3" "$4")" = "0" ]; then echo "  ok $1 ($2 / 期待 $3)"
	else echo "TUBE_FAIL: $1 = $2 / 期待 $3 (許容 $4)"; NG=1; fi
}

STRAIGHT='[[[0,0,0],0.5],[[5,0,0],0.5],[[10,0,0],0.5]]'
# ① 同一直線上 → 直管。閉形式 pi r^2 L = pi*0.25*10 = 7.8539816
ck "直管 = pi r^2 L" "$(val "print(\"VAL\", volume(\"occt\"::tube($STRAIGHT)));")" 7.853981633974483 1e-6

# ② 可変半径の直管 → 円錐台 pi L (r0^2 + r0 r1 + r1^2)/3 = pi*10*(0.25+0.5+1)/3 = 18.3259571
ck "円錐台の閉形式" \
   "$(val "print(\"VAL\", volume(\"occt\"::tube([[[0,0,0],0.5],[[10,0,0],1.0]])));")" \
   18.325957145940461 1e-6

# ③ ★ メッシュ側は segs を上げると occt の値へ収束する (= 別の形であることの示し方)。
C8=$(val "print(\"VAL\", volume(\"cgal\"::tube($STRAIGHT, 8)));")
C512=$(val "print(\"VAL\", volume(\"cgal\"::tube($STRAIGHT, 512)));")
if [ -n "$C8" ] && [ -n "$C512" ]; then
	R=$(awk -v a="$C8" -v b="$C512" -v x=7.853981633974483 \
	    'BEGIN{ da=(x-a); db=(x-b); if(da<0)da=-da; if(db<0)db=-db; print (db<da/10)?0:1 }')
	if [ "$R" = "0" ]; then echo "  ok cgal は segs を上げると occt へ収束 ($C8 → $C512)"
	else echo "TUBE_FAIL: cgal の収束が見えない ($C8 → $C512)"; NG=1; fi
else echo "TUBE_FAIL: cgal tube が値を出さない"; NG=1; fi

# ④ closed:1 で閉じた輪。点を増やすとスプラインが真円へ寄り、厳密トーラス 2 pi^2 R r^2 へ収束。
RING16='[[[5,0,0],0.5],[[4.619397662556,1.913417161826,0],0.5],[[3.535533905933,3.535533905933,0],0.5],[[1.913417161826,4.619397662556,0],0.5],[[0,5,0],0.5],[[-1.913417161826,4.619397662556,0],0.5],[[-3.535533905933,3.535533905933,0],0.5],[[-4.619397662556,1.913417161826,0],0.5],[[-5,0,0],0.5],[[-4.619397662556,-1.913417161826,0],0.5],[[-3.535533905933,-3.535533905933,0],0.5],[[-1.913417161826,-4.619397662556,0],0.5],[[0,-5,0],0.5],[[1.913417161826,-4.619397662556,0],0.5],[[3.535533905933,-3.535533905933,0],0.5],[[4.619397662556,-1.913417161826,0],0.5]]'
ck "closed:1 = 厳密トーラス" \
   "$(val "print(\"VAL\", volume(\"occt\"::tube($RING16, {closed:1})));")" \
   24.674011002723397 1e-3

# ⑤ 自己交差 → 明示エラー (既存 tube は許容する仕様なので、挙動が違うことを固定する)
case "$(msg "print(volume(\"occt\"::tube([[[0,0,0],1],[[10,0,0],1],[[10,0.2,0],1],[[0,0.2,0],1]])));")" in
	*"self-intersecting spine"*) echo "  ok 自己交差は明示エラー" ;;
	*) echo "TUBE_FAIL: 自己交差が明示エラーにならない"; NG=1 ;;
esac

# ⑥ ★ 曲がった管を自己交差と **誤検知しない** (半径 5 の輪を 16 点で書ける = ④ が通ること)。
#    ⑤ の判定は「空間で近い」だけでなく「経路上で離れている」ことも要る。

# ⑦ 半径 0 は明示エラー (メッシュ系は尖り端を許すが B-rep の円断面は作れない)
case "$(msg "print(volume(\"occt\"::tube([[[0,0,0],0],[[5,0,0],0.5]])));")" in
	*"radius > 0"*) echo "  ok 半径 0 は明示エラー" ;;
	*) echo "TUBE_FAIL: 半径 0 が明示エラーにならない"; NG=1 ;;
esac

# ⑧ closed で半径を変えるのは不可 (OCCT が閉背骨 + 複数断面で落ちるため・明示エラー)
case "$(msg "print(volume(\"occt\"::tube([[[5,0,0],0.5],[[0,5,0],0.8],[[-5,0,0],0.5],[[0,-5,0],0.8]], {closed:1})));")" in
	*"vary the radius along a closed spine"*) echo "  ok closed + 可変半径は明示エラー" ;;
	*) echo "TUBE_FAIL: closed + 可変半径が明示エラーにならない"; NG=1 ;;
esac

[ "$NG" -eq 0 ] && echo "OCCT-TUBE-OK"
exit "$NG"
