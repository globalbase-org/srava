# srava_hangwatch.sh — ハングの現物を捕まえる番犬 (全テストスクリプト共通)
#
#   使い方: テストスクリプトの冒頭 (MODE / D が決まった後) で
#       . "$(dirname "$0")/srava_hangwatch.sh"
#   を 1 行書くだけ。★ 常時 ON。SRAVA_HANG_SECS=0 で無効化できる。
#
#   ⚠ この 1 行を入れ忘れたスクリプトは**カバーされない**。2026-09-13 に
#     srava_cache_retain が 30 秒 TIMEOUT したのに撮れなかったのは、
#     番犬が srava_parse.sh にしか入っていなかったため。
#   MODE が未定義でもよい (ファイル名で代用する)。
[ -n "${MODE:-}" ] || MODE="$(basename "$0" .sh)"
[ -n "${D:-}" ] || D="${SRAVA_CACHE_DIR:-(unset)}"

# ★★ #3522: **ハングの現物を捕まえるための番犬**。常時 ON。
#   この harness は 205 テストの土台で、20 秒 TIMEOUT のハングはここでしか観測されていない。
#   ⚠ これまでは ctest が TIMEOUT で kill した**後**に気づいていたので、毎回証拠が消えていた。
#   ⇒ kill される前にスタックを撮る。次に 1 回起きれば答えが出る。
#
#   ★ 見張るのは **このスクリプト自身の PID ($$)**。多くのモードが `exec "$SRAVA"` で
#     自分自身を srava に置き換えるが、**exec は PID を変えない**ので、これ 1 つで
#     exec するモードもしないモードも両方カバーできる (srava を子で起こすモードでは、
#     このスクリプトが生きている間は srava も生きている)。
#
#   ★ 2 段構えにしてある。gdb のアタッチは対象を一瞬止めるので、**正当に遅いだけの**
#     テストに当てると、それが原因で TIMEOUT する恐れがある ([[観測が条件を動かす]])。
#       第 1 段 (SRAVA_HANG_SECS 秒)  … /proc を読むだけ。対象を止めない
#       第 2 段 (+4 秒)               … まだ生きていれば gdb。ここまで来たら本物とみなす
#
#   ⚠ TIMEOUT が長いテスト (pipeprox 等) は CMakeLists 側で SRAVA_HANG_SECS を上げること。
#     SRAVA_HANG_SECS=0 で無効化できる。
#   ⚠ 較正で回すときは **SRAVA_HANG_DIR を分ける** こと (本物が埋もれる)。
#   ★ 「撮れなかったデバッガを覚える」印 (.nodbg-*) の効き目: 1 つも撮れない機でも
#     ctest 464/464 が **137.9 秒** — 番犬が無かった頃の 137.5 秒と差が無い (2026-09-14 実測)。
#     ⇒ 印が無いと、10 秒より遅いテストのたびに 3 段 × 制限秒 × 対象数を燃やす。
#   ⚠ **撮れても他の印は消さない**。印は「そのデバッガが使えない」という *事実* なので、
#     別のデバッガが成功したからといって捨てる理由が無い (mac は lldb ○ / gdb × が同居する)。
#
# ---- ★★ 捕獲ファイルの読み方 (TIMEOUT を見たら **まずここを見る**) -----------------
#   ⚠ 番犬は **通知を出さない** (下の fd 切り離しの理由による) ⇒ 残るのはファイルだけ。
#     赤いテストを見たら $SRAVA_HANG_DIR (既定 /tmp/srava-hang) を自分で見に行くこと。
#   ⚠⚠ **run の前にこのディレクトリを消さないこと** — 2026-09-16 に bench で 2 回・macMINI で
#     1 回、**証拠を自分で捨てた** (「今回の run の捕獲だけ見たい」つもりで rm -rf した)。
#     ★ 消す必要は無い: ファイル名が `hang-<日付>-<時刻>-<mode>-<pid>.txt` なので
#       **捕獲どうしが上書きし合うことはない**。「この run のもの」は *run の開始時刻より
#       新しいもの* で選べばよい (例: `find $SRAVA_HANG_DIR -newermt "-10 min"`)。
#     ⚠ 分けたいのは **較正・実験のとき**だけで、そのときは消すのではなく
#       SRAVA_HANG_DIR を別にする (下の行)。
#   末尾の `=== 結論:` の行が、そのファイルが本物かどうかを言う。
#   ★★ State の規則は **Linux 専用**。3 通りある:
#       **T** (stopped) + TracerPid 0    = 番犬の人工物。中断された gdb が group-stop したまま
#                                          残したもの。`kill -CONT` で回収して捨てる (#3522 ではない)
#       **t** (tracing stop) + TracerPid≠0 = 採取の瞬間に番犬のデバッガが**まだ付いていた**。
#                                          ⚠ `t` は CONT では解けない (tracer が死ぬか detach
#                                          するまで)。⇒ tracer を殺せば自動で解ける = **一過性**
#       **S** (sleeping) + TracerPid 0    = **本物の待ち**
#     ⚠ mac には当てはまらない (/proc が無く、そもそも **撮れないのに T にもならない**)。
#
# ---- ★★ mac は **Developer mode が要る** (2026-09-14 に解決) -----------------------
#   ⚠⚠ 有効にするまで **3 手段とも srava に届かなかった**。番犬の欠陥ではなく機械側の設定:
#       gdb     arm64 macOS では attach 不可           "Don't know how to attach."
#       lldb    **拒否**                                "Not allowed to attach to process"
#       sample  bash / tail は撮れるのに srava では **返らない** (49 秒待って 0 バイト・状態 S)
#   ⇒ ★ `sudo DevToolsSecurity -enable` (一度きり・要管理者) で **lldb と sample の両方**が
#     撮れるようになった。⇒ sample が無言で寝ていたのも **同じ権限系**だった (lldb は即エラーを
#     返すのに、sample は理由を言わずに待つ ⇒ 症状からは同じ原因に見えなかった)。
#   ⚠ gdb は **設定では解けない** (arm64 の制約)。⇒ 3 段のうち 2 つが使える状態。
#   ⚠ 番犬が「撮れない」と言い出したら、まず `DevToolsSecurity -status` を見ること。
_hw_secs="${SRAVA_HANG_SECS:-10}"
if [ "$_hw_secs" != "0" ]; then
  _hw_dir="${SRAVA_HANG_DIR:-/tmp/srava-hang}"
  _hw_pid=$$
  (
    sleep "$_hw_secs"
    kill -0 "$_hw_pid" 2>/dev/null || exit 0
    # ⚠ PID 使い回しの誤爆よけ: 中身が本当にこのテストか確かめる
    # ⚠ PID 使い回しの誤爆よけ。/proc が無い環境 (macOS) では ps で代用する。
    if [ -r "/proc/$_hw_pid/cmdline" ]; then
      tr '\0' ' ' < "/proc/$_hw_pid/cmdline" 2>/dev/null | grep -q "srava" || exit 0
    else
      ps -o command= -p "$_hw_pid" 2>/dev/null | grep -q "srava" || exit 0
    fi
    # ★★★ #3522 (2026-09-20): **止まっている証拠を先に取る** — 空振りを撮らない。
    #
    #   ⚠⚠ それまでは「$_hw_secs 秒で 1 枚撮る」だけだった。box の -j4 走行で実測したら
    #     **捕獲 93 枚 / 対象 45 種すべてが最終的に緑** = 空振り率 100% だった。
    #     しかも 8 枚は採取後の状態が **R (runnable)** = 撮った時点で普通に走っていた。
    #   ⚠⚠ 空振りは無害ではない: 第 2 段の gdb は **対象を止める**ので、負荷が高いほど
    #     番犬が呼ばれ、番犬が遅さを作る、という**正のフィードバック**になる
    #     (観測コストが条件と相関する)。
    #
    #   ⇒ 撃つ前に **CPU 時間が進んでいないこと**を確かめる。経過時間ではなく CPU を見るのは、
    #     「遅いだけ」と「止まっている」を分ける唯一の指標だから。
    #   ★★ Windows でも取れる (2026-09-20 実測・box)。MSYS の /proc は **native な子にも**
    #     stat を出す。burn/sleep の対照で確かめた:
    #         回っている  utime 1359 -> 4468 ・ State R
    #         止まっている utime    0 ->    0 ・ State S
    #     ⚠ 旧コメントの「Windows では TIME が取れない (ps -W に列が無い)」は **ps の話**で、
    #       /proc には当てはまらなかった。判別は Windows でもできる。
    #   ⚠ 自分の子孫 (srava / srava_agent) も見る — 親のシェルは眠っていて当たり前なので、
    #     **一族の CPU の合計**で見る。
    # ⚠⚠ **`ps` に頼らない。** MSYS/Cygwin の ps は `-eo` を受け付けないので、
    #   `ps -eo pid,ppid` で子を数えると **常に 0 個**になり、
    #   「シェルは眠っている」= 止まっている、と誤判定する (2026-09-20 に box で実際に踏んだ:
    #    srava_install.sh が子の cmake を待って眠っている間に捕獲されていた)。
    #   ⇒ 親子も CPU も **/proc だけで引く**。/proc/<pid>/stat は
    #      $1=pid $2=comm $4=ppid $14=utime $15=stime ・ MSYS は native な子にも出す。
    # ⚠⚠ **comm (プロセス名) が位置をずらす** (2026-09-22 に dev-macmini-1 が指摘・実測で確認)。
    #   comm は `(…)` に囲まれた **プロセス名そのもの**で、空白も glob 文字も入りうる。
    #   素で `set -- $(cat …)` / `awk '{print $14+$15}'` すると:
    #     ① **空白**で語が増え、$4 が ppid でなくなる (実測: `(a b)` で $4=S ・ CPU 3000→1013)
    #     ② **glob** は *複数マッチのときだけ* ずれる (`(x1)` と `(x2)` が在って comm が `x[12]`
    #        の場合。単数マッチは語を書き換えるだけで語数は変わらない — 2026-09-22 に実測)
    #   しかも **どちらもエラーにならず、間違った親子関係/CPU が静かにできる**。ここは
    #   「一族の CPU が進んでいるか」= **番犬が撃つか**を決める所なので、ずれると判定を誤る。
    #   ⇒ ① pid は **ディレクトリ名**から取る (stat を読まない)
    #      ② comm は **最後の `) ` まで捨てる** (comm 内の `)` にも耐える)
    #         ⇒ 残りは元の第 3 フィールド以降なので **ppid=$2 ・ utime=$12 ・ stime=$13**
    #      ③ 残りは数値だけだが、**実行と分割を分けて** `set -f` で囲う (この木の作法)
    _hw_cpu() {        # 一族 (自分 + 子孫 2 段) の CPU jiffies 合計。取れなければ空文字
      _c_set=" $_hw_pid "
      # 子 → 孫 の 2 段ぶん広げる (srava の下に srava_agent が居る形まで届く)
      for _c_round in 1 2 ; do
        for _c_d in /proc/[0-9]* ; do
          [ -r "$_c_d/stat" ] || continue
          _c_st=$(cat "$_c_d/stat" 2>/dev/null)
          _c_rest=${_c_st##*\) }
          set -f
          set -- $_c_rest
          set +f
          _c_pid=${_c_d##*/}
          _c_ppid=$2
          [ -n "$_c_ppid" ] || continue
          case "$_c_set" in
            *" $_c_ppid "*)
              case "$_c_set" in *" $_c_pid "*) : ;; *) _c_set="$_c_set$_c_pid " ;; esac ;;
          esac
        done
      done
      _c_tot=""
      for _c_p in $_c_set ; do
        [ -r "/proc/$_c_p/stat" ] || continue
        _c_st=$(cat "/proc/$_c_p/stat" 2>/dev/null)
        _c_rest=${_c_st##*\) }
        set -f
        set -- $_c_rest
        set +f
        # 元の $14/$15 (utime/stime) = comm を落とした後の $12/$13
        [ -n "${12:-}" ] || continue
        _c_tot=$(( ${_c_tot:-0} + ${12:-0} + ${13:-0} ))
      done
      echo "$_c_tot"
    }
    _hw_cpu_a=$(_hw_cpu)
    if [ -n "$_hw_cpu_a" ]; then
      sleep 3
      kill -0 "$_hw_pid" 2>/dev/null || exit 0
      _hw_cpu_b=$(_hw_cpu)
      if [ -n "$_hw_cpu_b" ] && [ "$_hw_cpu_b" -gt "$_hw_cpu_a" ] 2>/dev/null; then
        # 進んでいる = 遅いだけ。**撮らない** (gdb で止めない)
        exit 0
      fi
    fi
    # ⚠ /proc が読めない機 (mac) では従来どおり撮る — 判別できないのに撮らないのは退行。
    mkdir -p "$_hw_dir" 2>/dev/null
    _f="$_hw_dir/hang-$(date '+%Y%m%d-%H%M%S')-$MODE-$_hw_pid.txt"
    # ★★ **結論は trap で必ず書く**。第 1 段と第 2 段の間 (や第 2 段の途中) で
    #   *テストが正常終了する*と、番犬の subshell が道連れになって結論が書かれない。
    #   ⚠⚠ その残骸は「デバッガが固まって書きかけで終わった」場合と **見分けが付かない**
    #     — 原因が違うのに残るものが同じになる (bench の実測・2026-09-14)。
    #   ⇒ 抜ける経路がどれでも 1 行は残す。⚠ SIGKILL では走らないので万能ではないが、
    #     正常終了・exit・TERM/HUP/INT は拾える。
    _hw_done=0
    _hw_finish() {
      [ "$_hw_done" = "1" ] && return 0
      [ -f "$_f" ] || return 0
      echo "=== 結論: ⚠ 第 2 段の前に番犬が終了した (テストが先に終わった = 正当に遅かっただけ" \
           "/ または番犬が外から止められた)。スタックは撮っていない" >> "$_f"
      _hw_done=1
    }
    # ★★ #3522 (2026-09-18): **ヘルパはここに置く (第 1 段より前)**。
    #   下の第 1 段が _hw_pidmap / _hw_ps_srava を呼ぶので、定義が後ろにあると
    #   `command not found` になり、**地図が空 = 子孫の判定が全部偽**・**対象 0 個** に化ける。
    #   ⚠ 2026-09-16 に box で出ていた `_hw_ps_srava: command not found` がこれ。
    #     エラーは捕獲ファイルに出るが、その下に「対象 0 個」と**もっともらしい行**が続くので
    #     読み飛ばされる。⇒ 位置を動かすときは第 1 段との前後関係を必ず見ること。
    # ★★ 2026-09-16: **MSYS / Cygwin の ps は `-o` 自体を受け付けない**
    #   (`ps: unknown option -- o`)。box で実測。さらに既定では **ネイティブの .exe が出ない**
    #   (MSYS のプロセスしか出さない) ので、`ps -W` で拾う必要がある。
    #   ⚠ そのため `ps -eo …` を素で使っている行は Windows で **静かに空**になる
    #     (2>/dev/null で握り潰されるため「srava が 0 個」と区別が付かない)。
    #   ⚠⚠ `ps -W` の COMMAND は **表記が混ざる**:
    #        /c/Users/joshu/.../srava      (MSYS 表記・.exe 無し)
    #        C:\Users\joshu\...\srava_agent.exe (Windows 表記・.exe 付き)
    #      ⇒ **区切りを / に揃え、basename を取り、.exe を落として**から比べる。
    #        素朴な一致は **常に 0 件**になる。
    #
    #   _hw_ps_srava   → "<pid> <ppid> <etime> <time> <stat> <comm>" (取れない列は -)
    #   _hw_pids_srava → srava / srava_agent の pid だけ
    # ★ 親子の地図。⚠ MSYS/Cygwin は `ps -eo` を受けないので **ps -W** で代替する
    #   (受けない機でこれが空になると、_hw_isdesc が全部偽になり **対象が 0 個**になる
    #    = 番犬が何も撮らない。2026-09-16 に box で踏んだ本丸)。
    _hw_pidmap() {
      _hw_m="$(ps -eo pid,ppid 2>/dev/null | awk 'NR>1{print $1":"$2}')"
      if [ -n "$_hw_m" ]; then printf '%s\n' "$_hw_m"; return 0; fi
      ps -W 2>/dev/null | awk 'NR>1{print $1":"$2}'
    }
    # ★ 実行体名で pid を引く (basename・.exe を落として比べる)。
    _hw_pids_named() {
      _hw_n="$(ps -eo pid,comm 2>/dev/null |
               awk -v w="$1" '{ n=split($2,a,"/"); b=a[n]; sub(/\.exe$/,"",b)
                                if (b==w) print $1 }')"
      if [ -n "$_hw_n" ]; then printf '%s\n' "$_hw_n"; return 0; fi
      ps -W 2>/dev/null |
        awk -v w="$1" 'NR>1 { c=$NF; gsub(/\\/,"/",c); n=split(c,a,"/"); b=a[n]
                              sub(/\.exe$/,"",b); if (b==w) print $1 }'
    }
    _hw_ps_srava() {
      _hw_o="$(ps -eo pid,ppid,etime,time,stat,comm 2>/dev/null | awk 'NR>1')"
      if [ -n "$_hw_o" ]; then
        printf '%s\n' "$_hw_o" |
          awk '{ c=$6; n=split(c,a,"/"); b=a[n]; sub(/\.exe$/,"",b)
                 if (b=="srava"||b=="srava_agent") print }'
        return 0
      fi
      # ⚠ ここへ来たら ps -eo が使えない機 (MSYS/Cygwin)。ps -W で代替する。
      #   列は PID PPID PGID WINPID TTY UID STIME COMMAND。**etime/time/stat は無い**
      #   ⇒ `-` を置く。TIME が無いので「回っているか」は ps では言えない (別の手が要る)。
      ps -W 2>/dev/null |
        awk 'NR>1 { c=$NF; gsub(/\\/,"/",c); n=split(c,a,"/"); b=a[n]; sub(/\.exe$/,"",b)
                    if (b=="srava"||b=="srava_agent")
                      printf "%s %s - - - %s\n", $1, $2, b }'
    }
    _hw_pids_srava() { _hw_ps_srava | awk '{print $1}'; }
    # ★★ #3522 (2026-09-18): **MSYS/Cygwin の pid と Windows の pid は別物**。
    #   gdb は native な Windows の実行体なので、MSYS の pid を渡しても **掴めない**。
    #   `ps -W` の 4 列目 (WINPID) を渡すこと。
    #   ⚠ これを渡していなかったため、box では毎回 attach に失敗し「このデバッガは使えない機」と
    #     学習されて .nodbg-* で 360 分沈黙していた (2026-09-18 に捕獲ファイルで確認)。
    #   ★ 同じ機で **手で WINPID を渡すと bt は撮れる** (#3522 の保全個体で実証済み) ⇒ 機体の
    #     問題ではなく渡し方の問題だった。
    #   ⚠ POSIX では `ps -W` が失敗して空になるので **そのまま元の pid を返す** (挙動不変)。
    _hw_winpid() {
      _hw_wp="$(ps -W 2>/dev/null | awk -v p="$1" 'NR>1 && $1==p {print $4; exit}')"
      if [ -n "$_hw_wp" ] && [ "$_hw_wp" != "$1" ]; then printf '%s\n' "$_hw_wp"
      else printf '%s\n' "$1"; fi
    }

    trap _hw_finish EXIT HUP TERM INT
    # ★★ **祖先の判定は第 1 段でも要る** (mac のバイナリ同一性が子孫の srava を探すため)。
    #   ⇒ 定義を前へ出した。⚠ 地図 (_hw_map) は第 2 段で撮り直す (その時点の親子関係で見る)。
    _hw_map=$(_hw_pidmap)
    _hw_isdesc() {          # $1 が $_hw_pid の子孫 (または本人) なら真
      _p="$1"; _n=0
      while [ -n "$_p" ] && [ "$_p" != "0" ] && [ "$_p" != "1" ] && [ "$_n" -lt 40 ]; do
        [ "$_p" = "$_hw_pid" ] && return 0
        _p=$(printf '%s\n' "$_hw_map" | sed -n "s/^$_p://p" | head -1)
        _n=$((_n+1))
      done
      return 1
    }
    {
      echo "=== srava hang watch"
      echo "date=$(date '+%F %T')  mode=$MODE  pid=$_hw_pid  after=${_hw_secs}s"
      echo "cache=$D"
      # ★★ **State を最初に出す。** T (stopped) なら #3522 ではなく **番犬の人工物** で、
      #   gdb が途中で死んで SIGSTOP されたまま残っただけ (SIGCONT で回収できる)。
      #   S なら本物の待ち。⚠ ps では両者が区別できないので、ここで明示する。
      if [ -r "/proc/$_hw_pid/status" ]; then
        grep -E "^(State|TracerPid|Threads|VmRSS|ShdPnd):" "/proc/$_hw_pid/status"
      else
        echo "State(ps)=$(ps -o stat= -p "$_hw_pid" 2>/dev/null)"
      fi
      # ---- fd の一覧 ------------------------------------------------------------
      # ⚠⚠ **2026-09-14 撤回**: 旧版はここで「両端を自分で握っている pipe」を数えて
      #   「2 と出たものが該当 (= close 漏れ)」と書いていた。**これは誤りだった。**
      #     ・`tsSignalCore.cpp:179` の**シグナル自己 pipe** は設計上 両端を持つ
      #       (ハンドラから write → イベントループで read の定番)。srava は起動時に数本持つ
      #     ・`uniq -c` は **同じ向きの dup も 2 と数える** (stdout/stderr が同じ pipe を指す等)
      #   ⇒ 健全な srava でも必ず「2」が並ぶ = **識別力ゼロ**。実際これで #3522 を誤診した。
      #   ★ 対照を取って分かった **本当の見分け方**:
      #       両端 (読+書 が揃う) … 自己 pipe。**正常**。居ても何も意味しない
      #       片端のみ            … agent との配管。**連番 inode 3 本で agent 1 個ぶん**
      #   ⇒ 異常を疑うのは「**片端 pipe が居るのに、対応する子プロセスが居ない**」とき。
      #     (保全個体は片端 0 本 = agent 0 個と整合し、漏れの証拠は無かった)
      #   flags: 読み端 = 04000 系 / 書き端 = 01。
      if [ -d "/proc/$_hw_pid/fd" ]; then
        echo "--- fd (flags: 読み端=04000 系 / 書き端=01):"
        for _fd in $(ls "/proc/$_hw_pid/fd" 2>/dev/null | sort -n); do
          _fl=$(awk '/^flags:/{print $2}' "/proc/$_hw_pid/fdinfo/$_fd" 2>/dev/null)
          echo "   $_fd -> $(readlink "/proc/$_hw_pid/fd/$_fd" 2>/dev/null)  flags=$_fl"
        done
        # pipe を inode ごとに「読み何本 / 書き何本」で分ける。
        echo "--- pipe の端の内訳 (両端=自己 pipe で正常 / ★片端のみ=agent 配管):"
        for _fd in $(ls "/proc/$_hw_pid/fd" 2>/dev/null); do
          _lk=$(readlink "/proc/$_hw_pid/fd/$_fd" 2>/dev/null)
          case "$_lk" in pipe:*) ;; *) continue ;; esac
          _fl=$(awk '/^flags:/{print $2}' "/proc/$_hw_pid/fdinfo/$_fd" 2>/dev/null)
          case "$_fl" in *4000*) echo "$_lk R" ;; *) echo "$_lk W" ;; esac
        done | sort | awk '{ c[$1 " " $2]++; k[$1]=1 }
          END { for (i in k) { r=c[i " R"]+0; w=c[i " W"]+0
                  printf "   %-20s 読み%d 書き%d%s\n", i, r, w,
                         (r>0 && w>0) ? "  (両端=自己 pipe・正常)" : "  ★片端のみ" } }'
      fi
      # ---- ★★ 実行中のバイナリとディスク上のバイナリが一致するか ----------------
      # ⚠⚠ **これが無いと bt を信じてよいか判断できない。** 2026-09-14 の保全個体は
      #   srava + libpig + モジュール 19 本の **21 個すべてが走行中に差し替えられて**いて、
      #   gdb が違うシンボルを載せた結果 **#5 以降が全部でたらめ**だった
      #   ("vtable for sPtr<tsApplication>" が呼び出し元として出る等)。
      #   ⇒ 捕獲ファイルだけ見ても気づけない。**その場で記録する。**
      #   ★ ついでに「テスト実行中に誰かがビルドした」の検出でもある (共有機では実際に起きる)。
      if [ -r "/proc/$_hw_pid/maps" ]; then
        _hw_del=$(awk '/\(deleted\)/{print $6}' "/proc/$_hw_pid/maps" 2>/dev/null | sort -u)
        if [ -n "$_hw_del" ]; then
          echo "--- ⚠⚠ バイナリが**走行中に差し替えられている** ⇒ gdb のシンボルは当てにならない:"
          printf '%s\n' "$_hw_del" | sed 's/^/     /'
        else
          echo "--- バイナリ一致 (走行中の差し替えなし) ⇒ bt のシンボルは信用してよい"
        fi
      fi
      # ---- ★★ mac 版の同じ問い (#3522・macMINI・2026-09-14) ------------------------
      # ⚠ /proc/PID/maps が無いので `(deleted)` は見られない。⇒ **起動時刻より新しい実行体・
      #   共有ライブラリ**を探す = 「**走行中に誰かがビルドした**」を直接測る。
      #   ★ 代理ではない — *ビルドが走ったか* そのものを見ている。
      #   ⚠ 「差し替えられたが内容は同一」までは区別しない (シンボルは合うので害が無い)。
      #   ★ 併せて **ディスク側の UUID** を控える。sample の出力末尾には
      #     "Binary Images:" として **プロセスに載っている側の UUID** が出るので、
      #     **両者を突き合わせれば同一性そのものを確かめられる** (警告を読むのではなく)。
      # ⚠⚠ `ps -o lstart=` は **ロケールで日本語化される** ⇒ date の解析が落ちる。LC_ALL=C 必須。
      #   (2026-09-14 に実際に踏んだ。[[macos-build-test-pitfalls]] の LC_ALL の話と同じ根)。
      if [ ! -r "/proc/$_hw_pid/maps" ] && [ -x /usr/bin/dwarfdump ]; then
        # ⚠ lstart は末尾に空白が付く (幅揃え) ので落とす。付いたままだと date が
        #   "Ignoring N extraneous characters" を **stdout ではなく stderr へ**出しつつ通るが、
        #   ⚠ 捕獲ファイルの文言が "2026     より新しい" と読みにくくなる。
        _hw_ls="$(LC_ALL=C ps -o lstart= -p "$_hw_pid" 2>/dev/null | sed 's/[[:space:]]*$//')"
        _hw_st="$(LC_ALL=C date -j -f '%a %b %d %T %Y' "$_hw_ls" '+%Y%m%d%H%M.%S' 2>/dev/null)"
        _hw_exe=""
        for _c in $(_hw_ps_srava | awk '$6=="srava"{print $1}'); do
          _hw_isdesc "$_c" 2>/dev/null || continue      # ★ 自分の子孫の srava だけ
          _hw_exe="$(ps -o comm= -p "$_c" 2>/dev/null)"; break
        done
        if [ -n "$_hw_exe" ] && [ -n "$_hw_st" ]; then
          _hw_bd="$(dirname "$_hw_exe")"
          _hw_ref="$_hw_dir/.ref-$$"
          if touch -t "$_hw_st" "$_hw_ref" 2>/dev/null; then
            _hw_new="$(find "$_hw_bd" -maxdepth 1 \( -name '*.so' -o -name '*.dylib' \
                            -o -name 'srava' -o -name 'srava_agent' \) \
                            -newer "$_hw_ref" 2>/dev/null)"
            if [ -n "$_hw_new" ]; then
              echo "--- ⚠⚠ **走行中に差し替えられたバイナリ** (起動 $_hw_ls より新しい)"
              echo "---    ⇒ デバッガのシンボルは当てにならない。bt を信じる前にここを見ること:"
              printf '%s\n' "$_hw_new" | sed 's/^/     /'
            else
              echo "--- バイナリ一致 (起動 $_hw_ls 以降にビルドされたものは無い) ⇒ シンボルは信用してよい"
            fi
            rm -f "$_hw_ref"
          else
            echo "--- ⚠ 起動時刻を取れず、バイナリの差し替えを確かめられなかった (lstart=$_hw_ls)"
          fi
          echo "--- ディスク側の UUID (sample 出力末尾の Binary Images と突き合わせる):"
          for _b in "$_hw_bd/srava" "$_hw_bd/libpig.dylib" "$_hw_bd/srava_agent"; do
            [ -f "$_b" ] || continue
            echo "     $(basename "$_b")  $(dwarfdump --uuid "$_b" 2>/dev/null | awk '{print $2}' | head -1)"
          done
        fi
      fi
      [ -r "/proc/$_hw_pid/cmdline" ] && { printf 'cmdline='; tr '\0' ' ' < "/proc/$_hw_pid/cmdline"; echo; }
      [ -r "/proc/$_hw_pid/stat" ]    && echo "state=$(awk '{print $3}' "/proc/$_hw_pid/stat") utime/stime=$(awk '{print $14, $15}' "/proc/$_hw_pid/stat")"
      [ -r "/proc/$_hw_pid/wchan" ]   && echo "wchan=$(cat "/proc/$_hw_pid/wchan")"
      # ★ /proc が無い環境 (macOS) 向けの代替。ps だけで state / CPU 時間 / コマンドが分かる。
      if [ ! -r "/proc/$_hw_pid/stat" ]; then
        echo "--- ps (/proc が無いので代替):"
        ps -o pid,stat,etime,time,command -p "$_hw_pid" 2>/dev/null
      fi
      for _t in /proc/$_hw_pid/task/[0-9]*; do
        [ -r "$_t/stat" ] || continue
        echo "  tid ${_t##*/} state=$(awk '{print $3}' "$_t/stat") wchan=$(cat "$_t/wchan" 2>/dev/null)"
      done
      # ⚠⚠ `ps --ppid` は **GNU 専用**。macOS では "illegal option" で、フォールバックの
      #   `ps -W` は Cygwin 用なので、mac では children が **常に空**だった (2026-09-14 に実測)。
      #   ★ 出力が空かで次へ落とす (終了コードでは判定しない — awk は空でも 0 を返す)。
      echo "--- children:"
      _hw_kids="$(ps --ppid "$_hw_pid" -o pid,etime,stat,cmd 2>/dev/null)"
      [ -n "$_hw_kids" ] || _hw_kids="$(ps -eo pid,ppid,etime,stat,command 2>/dev/null |
                                        awk -v p="$_hw_pid" '$2==p')"
      [ -n "$_hw_kids" ] || _hw_kids="$(ps -W 2>/dev/null | grep -i srava)"
      echo "$_hw_kids"
      # ★★ #3522 (2026-09-20・tinyState windows の指摘): **grep で名前を絞ると子を落とす**。
      #   上のフォールバック `ps -W | grep -i srava` は MSYS で `ps --ppid` / `ps -eo` が
      #   使えないための代替だが、**srava という名前が付いていない子は表に出ない**
      #   (ts2System が起こす子は agent とは限らず、cmd.exe / sh.exe / 外部ツールのことがある)。
      #   ⇒ 「agent が 1 本も居ない」は言えても「**子が 1 つも居ない**」の証明にならない。
      #   ⚠ これは #3556 の切り分けで実際に効いた: cache_path の捕獲で「agent 0 本」まで
      #     しか言えず、ts2System が子を待っている筋を落とせなかった。
      #   ⇒ Windows では **プロセス全表を ppid つきで撮る** (名前で絞らない)。
      #     Get-CimInstance なら MSYS の ps の制約 (書式指定不可) を丸ごと回避できる。
      #   ⚠ powershell の起動は 1 秒前後かかるが、捕獲は稀な事象なので許容する
      #     (番犬は 2026-09-20 から CPU 停止を確かめてからしか撮らない)。
      case "$(uname -s 2>/dev/null)" in
        MINGW*|MSYS*|CYGWIN*)
          echo "--- ★ プロセス全表 (ppid つき・名前で絞らない)"
          MSYS2_ARG_CONV_EXCL='*' powershell.exe -NoProfile -Command \
            "Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name | Sort-Object ParentProcessId,ProcessId | Format-Table -AutoSize | Out-String -Width 200" \
            2>/dev/null | sed 's/\r$//' | grep -vE "^$" | head -200
          ;;
      esac
      # ⚠⚠ macOS の `comm` は **フルパス**を返す (/Users/.../build/srava) ⇒ $5=="srava" が
      #   **一度も当たらない**。★ basename で比べれば Linux (元から basename) と両立する。
      # ★★ 2026-09-16: **TIME (CPU 時間) を足した**。ETIME (経過) だけでは
      #   「止まっている」と「10µs ループを回り続けている」が見分けられない
      #   (#3522 の FIN_THREAD_ROOT_LOOP 座り込みは後者の疑いが本命)。
      #   ⇒ 撮り直しの間に TIME が伸びていれば **回っている** = 勘定が 0 に戻らない側。
      #   ⚠ 列が 1 つ増えたので awk の comm は **$6** (旧 $5)。
      # ⚠ **ps では** Windows で TIME が取れない (2026-09-16・simu01 実測)。MinGW の ps は
      #   `-o` 自体を受けず (`ps: unknown option -- o`)、代わりに使える `ps -W` に
      #   **TIME 列が無い**。
      #   ★★ 2026-09-20 訂正: **/proc なら Windows でも取れる**。MSYS の procfs は native な
      #     子プロセスにも stat を出す (box で burn/sleep の対照を取って確認:
      #     回っている utime 1359→4468 / 止まっている 0→0)。⇒ 第 1 段の撮る/撮らないの
      #     判定はこれを使っている (上の _hw_cpu)。ここ (④ の表示) は ps 由来なので
      #     Windows では TIME 列が空のまま = 表示上の制約。**判別できないわけではない**。
      #
      # ---- ★★ なぜ ④ だけ **機体全体**を見るのか (2026-09-16・macMINI と合意) ----
      # ①②③ (189/500 行) が自分の子孫に限るのは **あれが *触る* 側だから** —
      # gdb を刺す・掴む行為が他人に及ぶと事故になる (500 行の直前に、別セッションの番犬が
      # 保全中のハング個体へ attach した記録が残っている)。④ は **読むだけ**なので、
      # その理屈は当てはまらない。そして ④ の狙い —「gdb が出す mutex の owner が
      # この一覧に *居なければ* 持ち主が消えた」— を言うには **広く見るほうが強い**。
      # ⚠⚠ ただし Linux の tid は **プロセスを跨いで一意**なので、無関係なプロセスの tid が
      #   owner の番号と一致しうる ⇒ 印が無いと「持ち主はまだ居る」と **誤って読める**。
      #   ⇒ **機体全体を見る + 各 pid に 子孫/他人 の印** の 2 本立てにする。
      # ⚠ 次の読み手へ: ここを ①②③ に合わせて「子孫だけ」に *直さないこと*。
      #   狙いが変わる。揃えたくなったら上の 3 段を読み直してほしい。
      # ★ #3522 (2026-09-18): ps -eo を直に書かず **_hw_ps_srava** を通す。MSYS/Cygwin は
      #   `ps -eo` 自体を受けないので、素で書くと **静かに空**になり「対象 0 個」に化ける。
      #   ヘルパは ps -W へ落ち、表記ゆれ (区切り・.exe) を正規化してから比べる。
      _hw_ps4=$(_hw_ps_srava)
      _hw_n4=$(printf '%s\n' "$_hw_ps4" | grep -c .)
      # ---- ★★ (a) **対象数を必ず書く** (2026-09-16・macMINI 指摘) ----
      # 以前は対象が 0 個のとき **何も書かなかった** ので、読む人は
      #   「対象が居なかった」  と  「④ が動かなかった」  を区別できなかった。
      # ★ box (Windows) で起きたのはまさにこれ。④ が「対象 0 個」と 1 行書いていれば、
      #   ps -eo が MinGW で通らないことは その場で見えていた。
      # ⚠ 番犬は赤/緑を返さない (rc に当たるものが無い) ⇒ **本文に書くしか無い**。
      # ---- ★★ (c) 地図が空なら **印を付けない** (2026-09-16・macMINI 指摘) ----
      # _hw_isdesc は地図 (_hw_map) が空だと **本人以外すべて偽**を返す。⇒ そのまま印を
      # 付けると「(子孫 0 / 他人 3)」という **データの形をした無根拠**が出る。
      # ★ これは (a) と同じ家系 — *入力が欠けたことが、自信のある答えに化ける*。
      #   印を足すことで、いま無い場所にその形を 1 つ増やしてしまう。⇒ 条件を付ける。
      if [ "$_hw_n4" = "0" ]; then
        echo "--- ④ srava / srava_agent: **対象 0 個** (ps が使えないか、走っていない)"
      elif [ -z "$_hw_map" ]; then
        echo "--- ④ srava / srava_agent: 対象 $_hw_n4 個 (⚠ 親子の地図 (ps -eo pid,ppid) が取れない ⇒ **帰属は不明**・印を付けない)"
        printf '%s\n' "$_hw_ps4" | sed 's/^/   /'
      else
        _hw_b4=$(printf '%s\n' "$_hw_ps4" | while read -r _l4; do
                   [ -n "$_l4" ] || continue
                   set -- $_l4
                   if _hw_isdesc "$1" 2>/dev/null; then echo "   [子孫] $_l4"
                   else                                 echo "   [他人] $_l4"; fi
                 done)
        echo "--- ④ srava / srava_agent: 対象 $_hw_n4 個 (子孫 $(printf '%s\n' "$_hw_b4" | grep -c '\[子孫\]') / 他人 $(printf '%s\n' "$_hw_b4" | grep -c '\[他人\]'))"
        printf '%s\n' "$_hw_b4"
      fi
      # ★★ 2026-09-16: srava の **生きている tid の一覧**。上の gdb が出す mutex の owner が
      #   ここに **居なければ「持ち主が消えた」**と言える (居るなら誰が握っているかが分かる)。
      #   ⚠ gdb を要しない。⇒ デバッガが使えない機体でも、この 1 点だけは取れる。
      #   ⚠ Linux 専用 (/proc)。mac は上の ps の一覧で代替する。
      # ★★ **owner の生死は「子孫」の印が付いた行だけで判断する** (上の (b) の理由)。
      # ⚠⚠ **印が無いとき (親子の地図が取れないとき) は、この判断ができない**。
      #   「子孫の行が 0 だった」= 持ち主が消えた、とは **読めない** — 帰属が不明なだけ。
      # ⚠ 印は「**地図 (_hw_map) を撮った瞬間の親子関係**」で決まる。⇒ その後に生まれた子は
      #   [他人] と印される。実機では地図を撮るのは sleep の後 (= 検分の時点) で agent は
      #   出揃っており、座り込み中に新しい agent は生まれないので実害は薄いが、
      #   **印は瞬間の写真であって、以後の親子関係ではない** (2026-09-16・macMINI が
      #   mac で ④ を発火させて確かめた際に見つけた限界)。
      _hw_mk() {   # $1=pid → 印。地図が無ければ **[帰属不明]** (無根拠な 子孫/他人 を作らない)
        [ -n "$_hw_map" ] || { printf '%s' "[帰属不明]"; return; }
        if _hw_isdesc "$1" 2>/dev/null; then printf '%s' "[子孫]"; else printf '%s' "[他人]"; fi
      }
      if [ -d /proc ]; then
        for _sp in $(_hw_pids_srava); do
          [ -d "/proc/$_sp/task" ] || continue
          echo "   $(_hw_mk "$_sp") pid $_sp の生きている tid: $(ls /proc/$_sp/task 2>/dev/null | tr '\n' ' ')"
        done
      else
        # ★ mac 版 (2026-09-16・macMINI)。⚠⚠ `ps -M` は **tid を出しません**
        #   (スレッド行の tid 欄が空白)。⇒ owner の照合には **使えない**。
        #   ★ ただし **スレッドの本数**は取れる。撤収の座り込みでは
        #     「main と gc_thread の 2 本だけ」= 第三者が居ないこと が効いた事実なので、
        #     その 1 点は mac でもこの行で取れる。
        #   ⚠ そもそも mac は **デバッガが attach できない** (Developer mode が有効でも
        #     "Not allowed to attach to process"・2026-09-16 実測) ので、
        #     上の gdb 由来の ① ② は mac では最初から出ない。⇒ tid が無くても損はしない。
        for _sp in $(ps -eo pid,comm 2>/dev/null |
                     awk '{ n=split($2,a,"/"); b=a[n]
                            if (b=="srava"||b=="srava_agent") print $1 }'); do
          _nt=$(ps -M -p "$_sp" 2>/dev/null | awk 'NR>1' | grep -c .)
          echo "   $(_hw_mk "$_sp") pid $_sp のスレッド数: ${_nt:-?}  (⚠ mac の ps -M は tid を出さない ⇒ owner の照合には使えない)"
        done
      fi
    } > "$_f" 2>&1
    # 第 2 段: まだ生きていれば本物 → スタックを撮る
    sleep 4
    kill -0 "$_hw_pid" 2>/dev/null || exit 0
    # ★★ デバッガは **名前ではなく「実際に撮れたか」で決める** (2026-09-14・macMINI)。
    # ⚠⚠ 「在るのに使えない」が **2 つ同時に**起きていた。mac の捕獲 5 件すべてが
    #   "Don't know how to attach." だけで終わり、スタックは 1 枚も撮れていなかった:
    #     ・gdb    homebrew で **入っている** (「mac に gdb は無い」という前提が外れていた)。
    #              だが **arm64 macOS の gdb は attach できない** ⇒ 最優先で選ばれて毎回詰む
    #     ・lldb   在るが **Developer mode が無効**だと拒否される
    #     ・sample **これだけ動く**が、gdb で詰むので到達しなかった
    # ⇒ command -v で 1 つ選ぶ方式だと**永久に直らない** (「在る/無い」ではなく
    #   「使える/使えない」が効くため)。★ 順に試し、フレームらしき出力が出なければ次へ落とす。
    # ⚠ 「Darwin なら gdb を飛ばす」の条件分岐にはしない — *在るのに使えない* は OS 名では
    #   言い当てられない (今回 lldb がまさにそれ)。
    # ★★ sObject の **fd 台帳** を読む (gdb のみ)。soPIPE/soOPEN は fd ごとに作成元の
    #   __FILE__:__LINE__ を `descriptor_list[]` に控えている ⇒ **fd の出所が一意に分かる**。
    #   これが「自己 pipe (tsSignalCore.cpp:179) / agent 配管 (ts2System.cpp:394,397,402) /
    #   pty (ts2System.cpp:366)」を区別できる**唯一の**手段。/proc の fd 一覧では区別できない。
    #
    #   ⚠⚠ **関数呼び出し (`call sObject::report_descriptor()`) にはしないこと。**
    #     あれは ::printf を使うので、対象が stdio ロックを握っていると **対象と gdb が両方固まる**。
    #     番犬の欠陥 ③④ (観測器が対象を壊す) と同じ轍になる。⇒ **メモリを読むだけ**にする。
    #   ⚠ Release ビルドには型情報が無く `p descriptor_list` は "has unknown type" で失敗する。
    #     ⇒ キャストで読む。CODE_POS の並びは { next; filename; line; subline; size } なので
    #       **x86-64 LP64 で filename=+8 / line=+16**。⚠ 他 ABI では合わない。
    #   ★ bt の **後ろ**に置く。台帳が引けない環境では gdb がそこで止まるので、前に置くと bt ごと失う。
    #   ⚠⚠ **上限は FD_SETSIZE。OS で違う** (2026-09-20 実測: Linux 1024 / MinGW **64**)。
    #     配列の実体は @descriptor_list[FD_SETSIZE]@ なので、1024 で舐めると **Windows では
    #     64 以降が配列外**になる。そこに偶然並んでいる CODE_POS 風のものを読んで、
    #     ★ **それらしい __FILE__:__LINE__ が 60 本以上並ぶ** (2026-09-20 の捕獲 2 枚が実際にそれ。
    #       index 120〜195 ・ 有効範囲の 0〜63 は **1 件も無かった**)。
    #     ⇒ 嘘の台帳を読んで「ptsErrSink が居残っている」と誤読した。上限は OS で決める。
    #   ⚠ Windows では **そもそも載らない**: set_open_hash が「winsock SOCKET / HANDLE は
    #     CRT fd でなく値が FD_SETSIZE を超えるので記録しない」と明記している。
    #     ⇒ **空が正常**。空を「異常が無い」と読まないこと (agent 配管・IOCP は台帳に出ない)。
    # ⚠⚠ **`echo` で書かないこと。** dash (= 多くの環境の /bin/sh) の echo は `\n` を
    #   **実際の改行に展開する**ので、gdb の printf が 3 行に割れて
    #   `Bad format string, non-terminated '"'` で落ちる。⚠ bash では展開されないため
    #   **手で試すと通り、番犬の中でだけ壊れる** (2026-09-14 に実際に踏んだ)。
    #   ⇒ クォート付きヒアドキュメント (置換が一切起きない) で書く。
    # FD_SETSIZE は OS で違う (Linux 1024 / MinGW・Cygwin 64)。**配列外を読まない**ための上限。
    case "$(uname -s 2>/dev/null)" in
      MINGW*|MSYS*|CYGWIN*) _HW_FDMAX=64 ;;
      *)                    _HW_FDMAX=1024 ;;
    esac
    _hw_gdbcmd() {   # $1 = 出力先の一時ファイル
      # ★ クォート付きヒアドキュメントは置換されないので、上限だけ **先に** 1 行書いておく。
      printf 'set $fdmax = %d\n' "$_HW_FDMAX" > "$1"
      cat >> "$1" <<'_HW_GDB_EOF'
set pagination off
# ★★★ 2026-09-17: **-ascending を付ける。** gdb の既定は **降順** (help に明記) なので
#   Thread N → … → Thread 1 の順に出る。⇒ 下の head -150 に **main (Thread 1) が必ず切られる**。
#   ⚠⚠ 2026-09-17 00:07 の捕獲 (srava_teardown_sysint) が実際にそれで、Thread 4/3/2 だけが
#     残り **いちばん要る main が丸ごと落ちていた**。しかも切られた印がどこにも出ていなかった。
#   ★ 「上限で切る」なら **いちばん要るものを先に出す** — targets の並び (srava を先に、sh を
#     最後に) と同じ理屈。
thread apply all -ascending bt full
printf "\n--- ★★ 撤収の座り込み (#3522): ワーカーの勘定と、待っている mutex の中身\n"
python
# ★★ 2026-09-16: FIN_THREAD_ROOT_LOOP の座り込み用。読むのは 2 つ。
#   ① tsThreadLiveWorkers  != 0 なら main は
#        if ( tsThreadLiveWorkers != 0 ) { HandleRelease(mtx); usleep(10); return rDO; }
#      を **永久に回る** (スレッドが main + gc の 2 本しか無いのに勘定が残っている、が本命の疑い)
#   ② 待っている mutex の owner/count/kind/lock
#      ⇒ owner が **生きていない tid** なら「持ち主が消えた」= app-mutex の解放漏れ
#        (tinyState.cpp:710 appMtxLock → 768 appMtxUnlock は try の外・手管理。sException 以外が
#         状態関数から抜けると 768 を飛ばす)
#      ⚠ owner が main 自身なら kind を見る — 再帰 mutex なら自分の錠で待つはずが無い
# ⚠⚠ **python で包む理由**: `thread apply all printf ..., futex_word` を素で書くと、
#   futex_wait に居ないスレッドで "No symbol" になり **コマンドファイルごと中断する**
#   (2026-09-16 に実測。後続の fd 台帳まで失う)。番犬は **シェルにも attach する**ので
#   tsThreadLiveWorkers が無い相手が普通に居る ⇒ 例外を握り潰せる python が要る。
# ⚠ 置き場所は **bt の後・fd 台帳の前**。台帳は引けない環境で gdb が止まる (下のコメント参照) ので、
#   その前に置かないとこのブロックごと失う。⇒ python の無い gdb では逆に台帳を失うが、
#   台帳が落ちる方が *起きると分かっている* 側なので、こちらを先にする。
import gdb
try:
    v = gdb.parse_and_eval("*(int*)&tsThreadLiveWorkers")
    print("   tsThreadLiveWorkers = %s   (!=0 = FIN_THREAD_ROOT_LOOP のループを抜けられない)" % v)
except Exception as e:
    print("   tsThreadLiveWorkers = (取れず: %s)" % e)
try:
    for t in gdb.selected_inferior().threads():
        try:
            t.switch()
            f = gdb.newest_frame()
            if f.name() and "futex" not in f.name():
                continue
            addr = int(gdb.parse_and_eval("futex_word"))
            print("   tid %d が待つ mutex 0x%x: owner=%s count=%s kind=%s lock=%s"
                  % (t.ptid[1], addr,
                     gdb.parse_and_eval("((int*)%d)[2]" % addr),
                     gdb.parse_and_eval("((int*)%d)[1]" % addr),
                     gdb.parse_and_eval("((int*)%d)[4]" % addr),
                     gdb.parse_and_eval("((int*)%d)[0]" % addr)))
        except Exception as e:
            print("   tid %s: (取れず: %s)" % (t.ptid[1], e))
except Exception as e:
    print("   スレッド一覧が取れず: %s" % e)
end
printf "\n--- ★ sObject fd 台帳 (fd -> その fd を作った行 ・ Windows は空が正常)\n"
set $i = 0
while $i < $fdmax
  set $cp = ((char**)&descriptor_list)[$i]
  if $cp != 0
    printf "   fd %4d <- %s:%d\n", $i, *(char**)($cp+8), *(int*)($cp+16)
  end
  set $i = $i + 1
end
_HW_GDB_EOF
    }
    # ★ sample 専用 (gdb / lldb は _hw_try が **直接 background にして $! を取る**ため、
    #   ここを経由しない — 経由すると $! が subshell を指してしまう)。
    _hw_dump() {   # _hw_dump sample <pid> → スタックらしきものを stdout へ
      case "$1" in
        sample) sample "$2" 2 -mayDie 2>&1 ;;
      esac
    }
    # 撮れたか。gdb/lldb は "#0 " / "frame #0" の行、sample は "Call graph:" が出る。
    _hw_is_bt() { grep -qE '^[[:space:]]*(#[0-9]+ |frame #[0-9]+)|^Call graph:' ; }
    # ⚠⚠ **デバッガ自身が固まる**。実測 (2026-09-14・mac): 対象が attach の直前に終了すると
    #   `lldb -p` が **38 秒経っても返らなかった**。⇒ 捕獲ファイルが書きかけで終わり、結論の行
    #   すら残らない = 番犬が番犬でなくなる。★ macOS に timeout(1) は無いので自前の番人で殺す
    #   (srava_hull.sh の run_limited と同じ形)。⚠ 番人の fd は閉じること。
    #   ⚠⚠ **sample だけ扱いが違う** (どちらも実測):
    #     ・背景に置くと **20 秒経っても終わらず出力 0 バイト**
    #     ・前景で叩くと撮れる…が **返らないことがある** (49 秒待って kill・状態は **S**)
    #       ⇒ SIGTTIN/SIGTTOU で止まっているのではなく、権限系で無言で寝ている
    #     ⇒ **前景で叩き、外から殺す番人**を立てる ($! が取れないので、子孫を辿って探す)。
    #   ⚠ 「sample は時間引数を取るから有界」と決めつけない — 有界なのは *採取時間* であって
    #     attach までの待ちではない。決めつけると、また撮れないまま気づけなくなる。
    # ⚠⚠ **殺す相手は pid で名指しする。パターンで機体上から拾わない。**
    #   ★ `pkill -f` は同一 uid のプロセスに届くので **射程がセッションを跨ぐ**。この機体では
    #     Claude のセッションが 3 つとも同じ利用者で走っている。⇒ ③ で潰した
    #     「名前で機体上から拾う」と **同じ形**になる (bench の指摘・2026-09-14)。
    #   ⇒ gdb / lldb は **直接 background にして $! を取る** (関数や ( ) を挟むと $! が
    #     subshell を指し、デバッガはその子になる = ①-C の機序そのもの)。
    #   ⇒ sample は前景でしか動かず $! が取れないので、**`_hw_isdesc` で子孫に限って**殺す
    #     ⇒ 「掴む対象」と「殺す対象」が **同じ規則**になる (規則が 2 つあると片方だけ直される)。
    # ★★ **打ち切ったことを呼び手へ持ち帰る**。⚠ さもないと「番犬が自分で殺した」のに
    #   *出力が空* → 「権限で黙って失敗」と **別の原因を名指し**し、印まで書いてしまう
    #   ⇒ **gdb が正常に使える機で 6 時間 gdb が飛ばされる** (bench の ①-C 検証で発覚)。
    #   ⚠ また「代理を見る」型 (*出力が空* を *使えない* の代理にした)。今日 3 つ目の同型。
    #   ★ 変数では渡せない (_hw_try は $( ) の中 = subshell) ので **合図のファイル**にする。
    _hw_killed_f() { echo "$_hw_dir/.killed-$$-$1-$2"; }
    _hw_try() {   # _hw_try <dbg> <pid> <制限秒> → 出力を stdout へ
      _hw_kf="$(_hw_killed_f "$1" "$2")"; rm -f "$_hw_kf" 2>/dev/null
      if [ "$1" = sample ]; then
        ( sleep "$3"
          _hw_map=$(_hw_pidmap)   # ★ 撮り直す
          for _sp in $(_hw_pids_named sample); do
            _hw_isdesc "$_sp" || continue      # ★ 自分の子孫の sample だけ
            kill -9 "$_sp" 2>/dev/null && : > "$_hw_kf"
          done ) >/dev/null 2>&1 </dev/null &
        _hw_wd=$!
        # ★ 2026-09-17: sample 側も **切ったことを書く** (gdb 側と同じ理由)。
        #   ⚠ 黙って切ると、読んだ人が「そのスレッドは居なかった」と読める。
        _hw_smp="$_hw_dir/.smp-$$-$2.tmp"
        _hw_dump sample "$2" > "$_hw_smp" 2>/dev/null
        sed "s/^Thread \([0-9][0-9]*\) /Thread \1 [pid $2] /" "$_hw_smp" 2>/dev/null | head -150
        if [ "$(wc -l < "$_hw_smp" 2>/dev/null || echo 0)" -gt 150 ]; then
          echo "--- ⚠ ここで **150 行で切りました** (全 $(wc -l < "$_hw_smp" 2>/dev/null) 行・sample)"
        fi
        rm -f "$_hw_smp"
        kill "$_hw_wd" 2>/dev/null
        return 0
      fi
      _hw_out="$_hw_dir/.dbg-$$-$2.tmp"
      # ★ **デバッガを直接 background にする** ⇒ $! がデバッガ本体を指す。
      case "$1" in
        gdb)  _hw_g="$_hw_dir/.hwgdb.$2.$$"
              _hw_gdbcmd "$_hw_g"       # ★ bt + fd 台帳 (-ex では while が書けないので -x)
              # ★ Windows では **WINPID** を渡す (POSIX では元の pid のまま・上の _hw_winpid 参照)
              _hw_tp="$(_hw_winpid "$2")"
              [ "$_hw_tp" = "$2" ] || echo "--- [gdb] pid $2 -> WINPID $_hw_tp で attach する"
              gdb -p "$_hw_tp" -batch -nx -x "$_hw_g" > "$_hw_out" 2>&1 </dev/null & ;;
        lldb) lldb -p "$2" --batch -o "thread backtrace all" -o "detach" \
                  > "$_hw_out" 2>&1 </dev/null & ;;
        *)    return 0 ;;
      esac
      _hw_dp=$!
      ( sleep "$3"; kill -9 "$_hw_dp" 2>/dev/null && : > "$_hw_kf" ) >/dev/null 2>&1 </dev/null &
      _hw_wd=$!
      wait "$_hw_dp" 2>/dev/null
      kill "$_hw_wd" 2>/dev/null
      # ★★★ 2026-09-17: **Thread の行に pid を書き足す。**
      #   ⚠⚠ 1 枚の捕獲には **複数のプロセス**の節が入る (srava と sh)。番犬は
      #     「どちらに答えが在るか」を事前に知らない — exec するモードでは見張っている pid が
      #     そのまま srava になり、しない モードでは sh のままで srava は子 — ので両方撮る。
      #   ⚠⚠ ところが gdb は **プロセスごとにスレッド番号を 1 から振り直す**ので、
      #     1 枚の中に `Thread 1` が複数出る。区切りは見出し 1 行だけなので、
      #     **`Thread` の行を拾った瞬間に帰属が消える**。
      #   ★ 2026-09-17 に実際に踏んだ: srava の main (Thread 1) が切り詰めで落ち、
      #     sh の main (Thread 1) だけが残った ⇒ **「Thread 1 は在る」と grep すると当たる**。
      #     いちばん誤解しやすい組み合わせだった。
      #   ⇒ ④ の帰属の印と同じ理屈で、**行そのものに持たせる**。
      sed "s/^Thread \([0-9][0-9]*\) /Thread \1 [pid $2] /" "$_hw_out" 2>/dev/null | head -150
      # ★★ 2026-09-17: **切ったことを書く。** 以前は黙って切っていたので、
      #   捕獲を読んだ人が「このスレッドは居なかった」と読めてしまった (実際に読んだ)。
      if [ "$(wc -l < "$_hw_out" 2>/dev/null || echo 0)" -gt 150 ]; then
        echo "--- ⚠ ここで **150 行で切りました** (全 $(wc -l < "$_hw_out" 2>/dev/null) 行)。"
        echo "      ★ -ascending なので **main (Thread 1) は上に在ります**。落ちたのは番号の大きいスレッド。"
      fi
      # ★★ 台帳は bt の **後ろ**に出る (gdb が台帳で失敗しても bt を失わないため) ので、
      #   この head -150 で**切り落とされる**。⚠ 実際 2026-09-14 の較正で落ちた。
      #   ⇒ 切られていたら台帳の節だけ別に拾う (head の上限はファイル肥大化の歯止めなので残す)。
      if [ "$(wc -l < "$_hw_out" 2>/dev/null || echo 0)" -gt 150 ]; then
        sed -n '/sObject fd 台帳/,$p' "$_hw_out" 2>/dev/null | head -80
      fi
      echo "--- pid $2 のスタック ここまで"   # ★ 節の閉じ括弧 (見出しだけだと切れ目が分からない)
      rm -f "$_hw_out"
      [ -n "$_hw_g" ] && { rm -f "$_hw_g"; _hw_g=""; }
    }
    # ★★ **一度 attach できなかったデバッガは、その機ではしばらく試さない**。
    #   ⚠ 番犬は 47 本すべてに入っていて、10 秒より遅いテストでは**必ず**第 2 段まで来る。
    #     撮れない機では 1 回の発火で 3 段 × 制限秒 × 対象数 = **30 秒以上デバッガを燃やす**。
    #     ctest 中の負荷と雑音になり、時間に敏感なテストを揺らしかねない。
    #   ★ 印は $_hw_dir に置く ⇒ **再起動で消える**。機械側を直したら rm で再挑戦。
    #   ⚠ **撮れても他の印は消さない** (真の情報を捨てないため・下の ⚠ を参照)。
    #   ⚠ 順は「速くて権限の要らないもの」から。sample は macOS にしか無いので、これは
    #     OS 名の分岐ではなく「sample が在る環境」という条件。
    # ★★ **印には寿命を付ける** (bench の指摘・2026-09-14)。事実は残しつつ、
    #   **事実が変わったことに気づける**形にする。今日そのものが反例だった:
    #     ① 設定前は lldb が拒否される ⇒ .nodbg-lldb が書かれる (正当な記録)
    #     ② ひさが DevToolsSecurity -enable ⇒ **機械が直る**
    #     ③ 印が残ったままだと、**撮れる手段が在るのに永久に撮らない**
    #   ⚠ しかも「撮れない ⇒ 捕獲ファイルが薄い ⇒ 誰も読まない ⇒ rm されない」の輪に入る。
    #     ⇒ 結論の行が rm を案内するだけでは **人が読むまで回復しない**。
    #   ⇒ 印が $SRAVA_HANG_DBG_TTL 分より古ければ **1 回だけ試す**。
    #     駄目なら印を書き直す (mtime が更新されてまた黙る) / 撮れたら **その印だけ**消す。
    #   ⚠ 「撮れたら印を全部消す」には戻さない — あれは *lldb で撮れた* を *gdb も直った* の
    #     代理にしていた (mac は lldb ○ / gdb × が同居する)。
    _hw_ttl="${SRAVA_HANG_DBG_TTL:-360}"          # 分。既定 6 時間
    _hw_cands=""; _hw_skipped=""; _hw_retry=""
    for _d in sample gdb lldb; do
      command -v "$_d" >/dev/null 2>&1 || continue
      if [ -f "$_hw_dir/.nodbg-$_d" ]; then
        if [ -n "$(find "$_hw_dir/.nodbg-$_d" -mmin +"$_hw_ttl" 2>/dev/null)" ]; then
          _hw_retry="$_hw_retry $_d"              # 期限切れ ⇒ 1 回試す
        else
          _hw_skipped="$_hw_skipped $_d"; continue
        fi
      fi
      _hw_cands="$_hw_cands $_d"
    done
    _hw_dbg=""
    [ -z "$_hw_skipped" ] || echo "--- ⚠ 前に attach できなかったので飛ばした:$_hw_skipped" \
        "(理由は $_hw_dir/.nodbg-*・すぐ再挑戦するなら rm・放っておいても ${_hw_ttl} 分で再挑戦)" >> "$_f"
    [ -z "$_hw_retry" ] || echo "--- ★ 印が ${_hw_ttl} 分より古いので再挑戦:$_hw_retry" >> "$_f"
    if [ -n "$_hw_cands" ]; then
      # ★ exec するモードでは $$ が srava 本体だが、exec しないモードでは sh のまま。
      #   ⚠ sh のスタックを撮っても意味がないので、**この harness の子孫にいる srava を探す**。
      # ⚠⚠ **実行体名 (comm) で照合すること。** cmdline の部分一致で "srava" を拾うと、
      #   コンパイラのコマンド行 (-DSRAVA_AGENT_DEFAULT=... 等) に当たって無関係な cc1plus を
      #   掴む。★ この罠はこのプロジェクトで 2026-09-12〜13 に **4 回**踏まれている。
      # ⚠⚠⚠ **必ず祖先で絞ること。** コメントには「この harness の子孫」と書いてあったのに
      #   実装に祖先の判定が無く、**機体上の srava を全部掴んでいた** (2026-09-14 に発覚)。
      #   共有機では Claude のセッションが同時にビルド/テストしているので、
      #   ★ 番犬が撃つたびに **他セッションの srava に gdb が刺さる**。
      #   ⇒ それ自体がハングを作りうる = **観測が対象を壊す**最悪の形だった。
      #   (実際、保全中のハング個体に別セッションの番犬が attach する事故が起きた。)
      _hw_map=$(_hw_pidmap)   # ★ 第 2 段の時点で撮り直す
      # ★ **srava_agent も対象に含める** (bench と合意・2026-09-14)。「子 (agent) が居るか」は
      #   最初に見る 3 点の 1 つなのに、居たときにスタックが撮れないのでは意味がない。
      #   ⚠ 子孫に限ってあるので他人の agent を掴む危険は無い。
      # ⚠ macOS の comm は **フルパス**なので basename で比べる (Linux は元から basename)。
      _hw_targets=""
      for _c in $(_hw_pids_srava); do
        [ "$_c" = "$_hw_pid" ] && continue
        _hw_isdesc "$_c" || continue          # ★ 他人の srava は掴まない
        case " $_hw_targets " in *" $_c "*) ;; *) _hw_targets="$_hw_targets $_c" ;; esac
      done
      # ★★ **srava を先に、sh を最後に**。デバッガは 1 対象に数秒かかるので、ctest が TIMEOUT で
      #   kill するまでに本命を撮り切れるかは順序で決まる。⚠ 実測 (2026-09-14): sh を先に撮って
      #   いる 2 秒の間に srava が終わり、kill -0 で弾かれて **本命を取り逃がした**。
      #   ⚠ $$ 自身が srava のとき (exec するモード) は先頭に置く。
      case "$(ps -o comm= -p "$_hw_pid" 2>/dev/null)" in
        */srava|srava|*/srava.exe|srava.exe) _hw_targets="$_hw_pid $_hw_targets" ;;
        *)                                   _hw_targets="$_hw_targets $_hw_pid" ;;
      esac
      # ⚠ agent が多いと撮り切る前に ctest が kill する。上限を置く (sh が押し出されるのは正しい)。
      _hw_targets="$(printf '%s\n' $_hw_targets | head -4 | tr '\n' ' ')"
      _hw_state() {   # _hw_state <pid> → State を 1 行で
        # ⚠ **空欄を出さない**。空が「終了した」のか「取得に失敗した」のか読めないと、
        #   State で人工物と本物を見分ける規則そのものが使えなくなる (bench の指摘・2026-09-14)。
        #   ★ 今日 6 件並んだ「代理を見て判断する」型を、ここでもう 1 つ作らないため。
        kill -0 "$1" 2>/dev/null || { echo "(終了済み)"; return 0; }
        if [ -r "/proc/$1/status" ]; then
          _hw_st="$(awk '/^State:|^TracerPid:/{printf "%s ", $0}' "/proc/$1/status")"
        else
          _hw_st="State(ps)=$(ps -o stat= -p "$1" 2>/dev/null)"
        fi
        case "$_hw_st" in
          *[!\ ]*) echo "$_hw_st" ;;
          *)        echo "(取得できず — 終了した直後か、ps/proc が答えなかった)" ;;
        esac
      }
      {
        echo "--- スタック採取 (T+$((_hw_secs+4))s) 対象: $_hw_targets  候補:$_hw_cands"
        for _t in $_hw_targets; do
          kill -0 "$_t" 2>/dev/null || continue
          # ★★ **採取の前後で State を比べる** (⑥ への追加・macMINI)。後だけ見ると、
          #   *元から T だった個体*を番犬の人工物と誤診する。前後があって初めて言い分けられる。
          echo "--- pid $_t 採取前: $(_hw_state "$_t")"
          if [ -r "/proc/$_t/cmdline" ]; then
            echo "--- pid $_t:  $(tr '\0' ' ' < /proc/$_t/cmdline 2>/dev/null)"
          else
            echo "--- pid $_t:  $(ps -o command= -p "$_t" 2>/dev/null)"
          fi
          for _d in $_hw_cands; do
            _o="$(_hw_try "$_d" "$_t" 15)"
            # ★★ **段ごとに起こす** (⑥ への追加・macMINI)。段を落とすとき、*前の段が止めた
            #   ままの対象*に次のデバッガを当てることになる。⚠ 制限秒で打ち切る経路では必須。
            kill -CONT "$_t" 2>/dev/null || :
            if printf '%s\n' "$_o" | _hw_is_bt; then
              echo "--- [$_d] ★撮れた"; printf '%s\n' "$_o"
              rm -f "$_hw_dir/.nodbg-$_d" 2>/dev/null   # ★ **その印だけ**消す (再挑戦が実った)
              # ⚠ **他のデバッガの印は消さない**。当初は「どれかで撮れたら全部消す
              #   (機械が直った合図)」にしていたが、**真の情報を捨てる**ことになる:
              #   この mac で DevToolsSecurity を入れると lldb だけが撮れるようになるが、
              #   gdb (arm64 で attach 不可) と sample (srava で返らない) は**依然として駄目**。
              #   全部消すと、撮れる機でも毎回 sample の制限秒を払い続けることになる。
              #   ⇒ 印は「そのデバッガが使えない」という事実。⚠ 機械側を直したときの回復は
              #     自動にせず、結論の行が案内する `rm $_hw_dir/.nodbg-*` に委ねる。
              _hw_dbg="$_d"; break
            fi
            # ⚠ **理由を残す**。この 3 段重ねが解けたのは "Don't know how to attach" が
            #   読めたから。次に別の理由で撮れないときも、同じ形で追える。
            _hw_why="$(printf '%s\n' "$_o" | grep -v '^$' | head -2)"
            # ★★ **こちらが打ち切った場合を最初に見る**。デバッガの欠陥ではなく
            #   *番犬の都合で切った* だけなので、原因を名指ししてはいけないし印も書かない。
            _hw_cut=0
            if [ -f "$(_hw_killed_f "$_d" "$_t")" ]; then
              _hw_cut=1; rm -f "$(_hw_killed_f "$_d" "$_t")" 2>/dev/null
              _hw_why="(**番犬が打ち切った** — 制限秒を超えた。デバッガの可否は不明)"
            elif [ -z "$_hw_why" ]; then
              # ⚠ 「対象が終了した」と「権限で黙って失敗」を **同じ文言に畳まない**
              #   (*出力が空* は *使えない* の代理でしかない)。生死で言い分ける。
              if kill -0 "$_t" 2>/dev/null
                then _hw_why="(出力なし — 対象は生きている ⇒ 権限で黙って失敗した疑い)"
                else _hw_why="(出力なし — **対象が採取中に終了した**)"
              fi
            fi
            echo "--- [$_d] ⚠撮れず: $_hw_why"
            # ★★ **一過性の失敗では印を書かない**。対象が消えただけなら、その機で
            #   デバッガが使えないことにはならない。
            #   ⚠⚠ Linux は gdb しか無いので、ここで一過性の失敗を覚えると
            #     **1 回で番犬が丸ごと沈黙する** (再起動か手動 rm まで)。bench の指摘・2026-09-14。
            # ★ 打ち切った回は印を書かない (デバッガの可否について何も言えていないので)。
            # ★★ 2026-09-16: **対象が srava でないときは印を書かない** (box で踏んだ)。
            #   MSYS の ps が srava を 1 つも返さない機では、対象がテストスクリプト (sh) だけに
            #   なる。sh に gdb を当てても C++ の bt は出ないので「撮れず」と判定され、
            #   **そのデバッガが使えないことにされて 360 分沈黙する**。
            #   ⚠ 2026-09-14 に足したガード (「対象が生きていれば印を書く」) は、
            #     **対象が間違っている場合を覆えていない** — 対象 (sh) は生きているので書かれる。
            #   ⇒ 印は「このデバッガは srava のスタックを撮れない」という主張なので、
            #     **srava を対象にしたときだけ**書いてよい。
            _hw_tn="$(_hw_ps_srava | awk -v t="$_t" '$1==t{print $6}')"
            if [ -z "$_hw_tn" ]; then
              echo "--- [$_d] ⚠ 印は書かない (対象 pid $_t は srava ではない)"
            elif [ "$_hw_cut" = "0" ] && kill -0 "$_t" 2>/dev/null; then
              printf '%s\n' "$_hw_why" > "$_hw_dir/.nodbg-$_d" 2>/dev/null
            fi
          done
          echo "--- pid $_t 採取後: $(_hw_state "$_t")"
        done
      } >> "$_f" 2>&1
    fi
    # ★ 結論を 1 行で書く。第 1 段は「10 秒より遅い」だけで撃つので、正当に遅いテストでも
    #   ファイルはできる。⇒ **雑音と本物をここで見分けられるようにする**。
    _hw_done=1
    if [ -n "$_hw_dbg" ]; then
      echo "=== 結論: $_hw_dbg でスタックを撮った (本物の疑い)" >> "$_f"
    elif [ -n "$_hw_skipped" ] && [ -z "$_hw_cands" ]; then
      echo "=== 結論: ⚠ この機では **前に全部 attach に失敗している**ので撮らなかった" \
           "(飛ばした:$_hw_skipped)。機械側を直したら rm $_hw_dir/.nodbg-* で再挑戦する" >> "$_f"
    elif [ -z "$_hw_cands" ]; then
      echo "=== 結論: デバッガ (gdb / lldb / sample) が 1 つも無く、スタックは撮れなかった" >> "$_f"
    else
      echo "=== 結論: ⚠ スタックは撮れなかった (試した:$_hw_cands)。" \
           "対象が T+$((_hw_secs+4))s までに終わったか、どのデバッガも attach できない機。" >> "$_f"
    fi
    # ★★ **必ず SIGCONT を送る。** gdb が正常に detach しても、途中で死んでも、
    #   対象が SIGSTOP されたまま残ることがある。⚠ その残骸は State=T / TracerPid=0 で、
    #   **ps でも ctest でもハングと区別が付かない** (2026-09-14 に bench が実証)。
    #   ⇒ 撮り終わったら無条件に起こす。止まっていなければ無害。
    for _t in ${_hw_targets:-$_hw_pid}; do kill -CONT "$_t" 2>/dev/null; done
    echo "[srava hangwatch] ★ハング捕獲: $_f" >&2
  ) >/dev/null 2>&1 </dev/null &
fi
# ⚠⚠ 上の subshell は **必ず fd を切り離す** (>/dev/null 2>&1 </dev/null)。
#   ctest はテストの出力パイプが閉じるまで待つので、番犬が stdout を握ったままだと
#   **テストが終わっても sleep のぶんだけ待たされる**。
#   ★ これを忘れて全テストが 0.3 秒 → 10 秒になり、スイート総時間が 287 秒 → 671 秒になった
#     (2026-09-13・[[観測コストが条件と相関する]] の別の顔)。
