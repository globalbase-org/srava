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
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
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
C8=$(val "print(\"VAL\", volume(\"cgal\"::tube_ruled($STRAIGHT, 8)));")
C512=$(val "print(\"VAL\", volume(\"cgal\"::tube_ruled($STRAIGHT, 512)));")
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

# ================================================================================
# ★★★ #3593: tube_ruled (折れ線の背骨 + 厳密な円の断面)。
#   掃引系は「なめらか / 線織」の対で揃える — loft / loft_ruled は対だったのに tube だけ
#   occt が線織版を持たなかった。⇒ 足した。分かれ目は **背骨だけ**で断面は厳密な円のまま。
# ================================================================================

RULED_L='[[[0,0,0],1],[[10,0,0],1],[[10,10,0],1]]'

# ⑨ 直管は背骨が折れていないので **tube と同じ閉形式**に一致する (pi r^2 L)。
ck "tube_ruled 直管 = pi r^2 L" \
   "$(val "print(\"VAL\", volume(\"occt\"::tube_ruled($STRAIGHT)));")" 7.853981633974483 1e-9

# ⑩ 可変半径の直管 → 円錐台の閉形式。★ 断面が厳密な円なので **9 桁**で合う。
ck "tube_ruled 円錐台の閉形式" \
   "$(val "print(\"VAL\", volume(\"occt\"::tube_ruled([[[0,0,0],0.5],[[10,0,0],1.0]])));")" \
   18.325957145940461 1e-9

# ⑪ ★★ **本命**: 角のあるパスでは tube と tube_ruled が *別の形* になる。
#    スプラインの背骨は角を丸めるので体積が膨らむ。
#    ⇒ 代用が効かないことをここで固定する (これが #3593 を起票した理由そのもの)。
VT=$(val "print(\"VAL\", volume(\"occt\"::tube($RULED_L)));")
VR=$(val "print(\"VAL\", volume(\"occt\"::tube_ruled($RULED_L)));")
if [ -n "$VT" ] && [ -n "$VR" ]; then
	R=$(awk -v a="$VT" -v b="$VR" 'BEGIN{ if(b<=0){print 1; exit} d=(a-b)/b; print (d>0.1)?0:1 }')
	if [ "$R" = "0" ]; then echo "  ok 角では tube と tube_ruled が別の形 ($VT 対 $VR)"
	else echo "TUBE_FAIL: 角で tube と tube_ruled の差が出ない ($VT 対 $VR)"; NG=1; fi
else echo "TUBE_FAIL: L 字の tube / tube_ruled が値を出さない"; NG=1; fi

# ⑫ ★★★ **cgal の tube_ruled は segs を上げると occt の tube_ruled へ収束する**。
#    これが「同じ構成 (折れ線の背骨 + リング間を線織) で、断面が n 角形か円かだけが違う」
#    ことの示し方。⚠ 逆に言うと **値は一致しない**ので kernel_agree には入れない。
#    ★ 実装の要: occt 側は MakePipeShell ではなく **ThruSections(ruled)** で組む。
#      MakePipeShell は角の処理を自分で持っていて、こちらが置いた断面と二重に効き、
#      遷移モードをどれに変えても cgal の極限からは外れる。
if [ -n "$VR" ]; then
	G8=$(val "print(\"VAL\", volume(\"cgal\"::tube_ruled($RULED_L, 8)));")
	G512=$(val "print(\"VAL\", volume(\"cgal\"::tube_ruled($RULED_L, 512)));")
	if [ -n "$G8" ] && [ -n "$G512" ]; then
		R=$(awk -v a="$G8" -v b="$G512" -v x="$VR" \
		    'BEGIN{ da=(x-a); db=(x-b); if(da<0)da=-da; if(db<0)db=-db; print (db<da/10)?0:1 }')
		if [ "$R" = "0" ]; then echo "  ok cgal tube_ruled は segs を上げると occt tube_ruled へ収束 ($G8 → $G512 / occt $VR)"
		else echo "TUBE_FAIL: cgal tube_ruled が occt tube_ruled へ収束しない ($G8 → $G512 / occt $VR)"; NG=1; fi
	else echo "TUBE_FAIL: cgal tube_ruled が値を出さない"; NG=1; fi
fi

# ⑬ ★ **能力差を固定する**: closed + 可変半径は tube では明示エラー (⑧) だが、
#    tube_ruled では **通る**。実装機構が違うため (MakePipeShell は閉背骨 + 複数断面で
#    落ちるが、ThruSections は断面を順に並べるだけなので閉じた輪でも半径を変えられる)。
CV=$(val "print(\"VAL\", volume(\"occt\"::tube_ruled([[[5,0,0],0.5],[[0,5,0],0.8],[[-5,0,0],0.5],[[0,-5,0],0.8]], {closed:1})));")
if [ -n "$CV" ]; then
	R=$(awk -v a="$CV" 'BEGIN{ print (a>0)?0:1 }')
	if [ "$R" = "0" ]; then echo "  ok closed + 可変半径は tube_ruled では通る ($CV)"
	else echo "TUBE_FAIL: closed + 可変半径の tube_ruled が正の体積を返さない ($CV)"; NG=1; fi
else echo "TUBE_FAIL: closed + 可変半径の tube_ruled が値を出さない"; NG=1; fi

# ⑮ ★★★ #3594: **2D のパスは tube_ruled では明示エラー**。
#    tube_ruled(path) の意味は 8 カーネルで確定していて、位置が [x,y] なら結果は **2D の領域**。
#    occt は常に 3D の立体を名乗るので、[x,y] を z=0 と読んで掃くと *同じ式がカーネルによって
#    「帯」と「平たい立体」に化ける* = #3588 で直したのと同じ「黙って別のものが返る」形になる。
#    ⇒ 新しい意味を作らず明示エラーにする。
#    ⚠ 文言は「occt には作れない」ではなく「**この op がまだ 3D 専用**」— occt 自体は 2D の
#      領域を持てる (rect / circle / polygon / 2D の offset)。能力の限界と読ませない。
#    ⚠ 測るのは **実値** (volume)。type_of は宣言型を返すので、この種の食い違いは見えない。
case "$(msg "print(volume(\"occt\"::tube_ruled([[[0,0],1],[[10,0],1],[[10,10],1]])));")" in
	*"3D-only"*) echo "  ok tube_ruled の 2D パスは明示エラー" ;;
	*) echo "TUBE_FAIL: tube_ruled が 2D パスを黙って 3D の立体にしている"; NG=1 ;;
esac

# ⑯ ★ 対照: **occt 単独の op である tube は従来どおり [x,y] を z=0 として受ける**。
#    あちらは他カーネルに相手が居ないので突き合わせる規約が無い。⇒ ⑮ は tube には及ばない。
CT=$(val "print(\"VAL\", volume(\"occt\"::tube([[[0,0],1],[[10,0],1],[[10,10],1]])));")
if [ -n "$CT" ]; then
	R=$(awk -v a="$CT" 'BEGIN{ print (a>0)?0:1 }')
	if [ "$R" = "0" ]; then echo "  ok tube は 2D 位置を z=0 として受ける ($CT)"
	else echo "TUBE_FAIL: tube が 2D 位置で正の体積を返さない ($CT)"; NG=1; fi
else echo "TUBE_FAIL: tube が 2D 位置で値を出さない"; NG=1; fi

# ⑭ 半径 0 は tube_ruled でも明示エラー (B-rep の円断面は半径 0 を作れない)
case "$(msg "print(volume(\"occt\"::tube_ruled([[[0,0,0],0],[[5,0,0],0.5]])));")" in
	*"radius > 0"*) echo "  ok tube_ruled でも半径 0 は明示エラー" ;;
	*) echo "TUBE_FAIL: tube_ruled で半径 0 が明示エラーにならない"; NG=1 ;;
esac

[ "$NG" -eq 0 ] && echo "OCCT-TUBE-OK"
exit "$NG"
