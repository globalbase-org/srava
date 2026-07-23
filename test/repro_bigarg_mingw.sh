#!/bin/sh
# repro_bigarg_mingw — srava #4「性能崖」の MinGW 再発の最小再現。
#
# 現象: planner→agent に **大きなインライン引数(C_ARG_INLINE)** を送ると、その **serialize 後の
#   payload が ~64KB(named pipe buffer)を超えた瞬間** に planner の送信(ptsWirePipe write_record→
#   wio->write_c)が止まり、agent が入力を受け取り切れず計算に至らない(cache 出力ゼロ=入力側 stall)。
#   MinGW 実機(NucBox7 MINGW64)で発現。Linux では tinyState #3365 修正後に解消済み。
#
# 切り分け結果(この repro で確認):
#   - 入力側: stall 時、SRAVA_CACHE_DIR に agent の mesh キャッシュが**一切出ない**(info.txt のみ)。
#   - サイズ依存: tube の点数を増やすと source ~25KB(=serialize 後 ~64KB)付近で OK→STALL に転じる。
#   - tinyState 単体(raw write_c + 64KB チャンク read の drain child)は 2MB まで健全(#3393 WontFix)。
#     差は srava の **ptsWirePipe record framing + agent の read_c(全 payload 長)** 経路。
#
# 依存: python3 不要(awk で配列生成)。cygpath(Windows でパス正規化)。
# 使い方: SRAVA_AGENT=<srava_agent> sh test/repro_bigarg_mingw.sh <srava>
#   (env: SRAVA_AGENT 必須。無ければ srava と同 dir の srava_agent を推測)
set -u
SRAVA="${1:?usage: repro_bigarg_mingw.sh <srava>}"
AG="${SRAVA_AGENT:-$(dirname "$SRAVA")/srava_agent}"
[ -x "$AG" ] || AG="$AG.exe"

TMP=/tmp
command -v cygpath >/dev/null 2>&1 && TMP=$(cygpath -m /tmp)   # Windows: native /tmp

gen() {  # $1=点数 → tube 用 [[[x,y,z],r],...] を生成(cos/sin は awk)
  awk -v n="$1" 'BEGIN{s="";for(i=0;i<n;i++){if(i)s=s",";
    s=s sprintf("[[%.4f,%.4f,%.4f],0.3]",cos(i*0.05),sin(i*0.05),i*0.02)}print s}'
}

# NB: MinGW での stall は **間欠的(双方向パイプ・デッドロックの race)**。時々完走・時々 hang するので
#     各サイズを ATTEMPTS 回試し、1 回でも hang したら STALL とみなす。Linux は毎回即完走(<1s)。
TO="timeout 30"; command -v timeout >/dev/null 2>&1 || TO=""   # Linux 0.3s / MinGW hang を弾く
ATTEMPTS="${REPRO_ATTEMPTS:-4}"

echo "# repro_bigarg_mingw: 大インライン引数の送信 stall を点数×${ATTEMPTS}回試行で探る"
rc_any_stall=0
for n in 256 512 900 1024 2048; do
  PTS=$(gen "$n")
  bytes=$(printf '%s' "$PTS" | wc -c | tr -d ' ')
  hung=0; slow=0; a=0
  while [ "$a" -lt "$ATTEMPTS" ]; do
    a=$((a+1))
    D="$TMP/repro-bigarg-$n-$a"; rm -rf "$D"
    t0=$(date +%s 2>/dev/null || echo 0)
    SRAVA_AGENT="$AG" SRAVA_CACHE_DIR="$D" \
      SRAVA_SOURCE="export(\"$D.off\", tube([$PTS], 6));" \
      $TO "$SRAVA" >/dev/null 2>&1
    rc=$?
    t1=$(date +%s 2>/dev/null || echo 0)
    meshc=$(ls "$D"/*.cache 2>/dev/null | wc -l | tr -d ' ')
    if [ "$rc" = 124 ]; then hung=$((hung+1)); [ "$meshc" = 0 ] && slow=$slow; fi
    rm -rf "$D"
  done
  if [ "$hung" -gt 0 ]; then
    echo "  n=$n arg=${bytes}B : ***STALL*** ($hung/$ATTEMPTS 回 hang・agent mesh cache 未出=入力側)"
    rc_any_stall=1
  else
    echo "  n=$n arg=${bytes}B : ok ($ATTEMPTS/$ATTEMPTS 完走)"
  fi
done
[ "$rc_any_stall" = 1 ] && echo "REPRO: 大インライン引数 stall を再現(srava #4 性能崖 / MinGW・間欠 deadlock)" \
                        || echo "NO-REPRO: 全試行で完走(Linux 期待)"
exit 0
