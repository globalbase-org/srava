#!/bin/sh
# in-process 評価器 (#3406 4.3 / ptsMediatorInternal) の回帰テスト。
# $1 = srava 実行体。$2 = モード(value|mesh)。env: SRAVA_AGENT_MF, SRAVA_CACHE_DIR。
#
# ★ このテストの存在理由 (2026-08-05):
#   ctest の既定カーネルは CGAL なので、SRAVA_INPROC=1 を付けて全 176 テストを回しても
#   agent_kernel_name() が thNULL を返して **全部 External に落ちる** = Internal 経路は
#   1 行も実行されない。実際 §5/§6 で実行体から a_write を撤去したとき、ptsMediatorInternal
#   だけ追随できておらず INPROC が全 op で "agent closed before save done" になっていたのに、
#   ctest は 176/176 green のままだった。よって **DEFAULT_OUTPUT=manifold を明示**して
#   Internal 経路を必ず踏むテストを別に持つ。
#   ★INPROC 既定 ON (2026-08-06) 後: External 側は SRAVA_INPROC=0 の明示 opt-out で踏む
#   (明示しないと両方 in-proc になり、何も比較しないテストになる)。
#
# 検証の形: 同じプログラムを External と INPROC で走らせ **結果の一致**を見る。
#   Internal は「planner と agent が pigDataCache を共有し、値は pigData のまま直渡し」なので、
#   ワイヤ経由の External と同じ答えになることが唯一かつ十分な契約。
#   ⚠ 2 回の実行で **キャッシュ dir を必ず分ける**。共有すると 2 回目が全 HIT になり
#     agent が起動せず、何も検証しないテストになる。
SRAVA="$1"
MODE="${2:-value}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
T=/tmp
if command -v cygpath >/dev/null 2>&1; then T=$(cygpath -m /tmp); D=$(cygpath -m "$D"); fi
rm -rf "$D" "$D-ext" "$D-inp"

case "$MODE" in
value)
	# 値返し op (volume) = A_SAVE_BEGIN に構造化 pigData が相乗りする経路。
	# ★ .so 化 Phase 4c: env DEFAULT_OUTPUT/SRAVA_INPROC を撤去。manifold 選択 + 実行方式は
	#   module("manifold.so",{priority, exec_default}) で撃ち分ける (External=process / in-proc=thread)。
	S='var m = box(2,2,2) ||| box(1,1,3); print("VOL", volume(m));'
	EXT=$(SRAVA_CACHE_DIR="$D-ext" \
	      SRAVA_SOURCE="module(\"manifold.so\",{priority:99,exec_default:\"process\"}); $S" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	INP=$(SRAVA_CACHE_DIR="$D-inp" \
	      SRAVA_SOURCE="module(\"manifold.so\",{priority:99,exec_default:\"thread\"}); $S" "$SRAVA" 2>&1 | sed -n 's/^VOL //p')
	if [ -z "$EXT" ]; then echo "FAIL: External produced no volume"; exit 1; fi
	if [ -z "$INP" ]; then echo "FAIL: INPROC produced no volume"; exit 1; fi
	ok=$(awk -v a="$EXT" -v b="$INP" 'BEGIN{
		d=a-b; if(d<0)d=-d; s=(a<0?-a:a); if(s<1)s=1;
		print (d <= 1e-9*s) ? 1 : 0 }')
	if [ "$ok" != "1" ]; then
		echo "FAIL: volume mismatch External=$EXT INPROC=$INP"; exit 1
	fi
	echo "INPROC-VALUE-OK $INP" ;;
mesh)
	# mesh 返し op = 本文がストリーム系 (D_CHUNK) で A_SAVE_BEGIN は空、planner は共有
	# pigDataCache ハンドルで受ける経路。書き出した STL の三角形数で一致を見る。
	EO="$T/srava-inproc-ext.stl"; IO="$T/srava-inproc-inp.stl"
	rm -f "$EO" "$IO"
	SRAVA_CACHE_DIR="$D-ext" \
	  SRAVA_SOURCE="module(\"manifold.so\",{priority:99,exec_default:\"process\"}); export(\"$EO\", box(2,2,2) ||| box(1,1,3));" "$SRAVA" >/dev/null 2>&1
	SRAVA_CACHE_DIR="$D-inp" \
	  SRAVA_SOURCE="module(\"manifold.so\",{priority:99,exec_default:\"thread\"}); export(\"$IO\", box(2,2,2) ||| box(1,1,3));" "$SRAVA" >/dev/null 2>&1
	if [ ! -f "$EO" ]; then echo "FAIL: External wrote no stl"; exit 1; fi
	if [ ! -f "$IO" ]; then echo "FAIL: INPROC wrote no stl"; exit 1; fi
	n() { python3 -c 'import struct,sys; b=open(sys.argv[1],"rb").read(); print(struct.unpack_from("<I",b,80)[0])' "$1"; }
	NE=$(n "$EO"); NI=$(n "$IO")
	if [ "$NE" != "$NI" ]; then
		echo "FAIL: tri count mismatch External=$NE INPROC=$NI"; exit 1
	fi
	echo "INPROC-MESH-OK $NI" ;;
*)
	echo "unknown mode: $MODE"; exit 2 ;;
esac
