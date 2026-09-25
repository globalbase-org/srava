#!/bin/sh
# srava_agent_eof_before_end.sh — **すべてのモジュール**について、agent が
# `C_ARG_END` を受け取る前に標準入力が切れたら **自分で終了する** ことを留める。
#
#   引数: $1 = srava 実行体 / $2 = srava_agent 実行体
#   env : SRAVA_CACHE_DIR (作業ディレクトリを作る場所として使う)
#
# ★ なぜ機械で留めるか
#   planner は「もう要らない agent」を **wfd を閉じて EOF で畳む** 形で撤収する。
#   実例 (a4877e7 / #3591): 遅延引数の経路でキャッシュ HIT が判った時点で、既に fork 済みの
#   agent を `FIN_START → med->destroy() → wfd を閉じる` で撤収する。piggyback (#3591 以前から)
#   と in-flight dedup も同じ経路に乗る。**どれも C_ARG_END の前**なので、agent 側が EOF で
#   畳まれなければ、撤収したつもりの agent が残る。
#   ⇒ これは agent host (srava_agent) の性質だが、**モジュールごとに .so を dlopen した状態**で
#     待つので、モジュールが張った資源が EOF 経路を塞ぐと 1 本だけ落ちうる。だから全数で見る。
#
# ★★ 較正を各モジュールに同梱してある (これがこのテストの肝)
#   「終了した」は、**その前に生きていたこと**を示さないと意味を持たない。
#   モジュールのロードに失敗した agent は EOF と無関係に即死するので、較正を置かないと
#   **壊れているモジュールほど緑になる**。⇒ 各モジュールで先に
#     ① EOF を与えなければ **切られるまで生きている** (timeout に殺される = rc 124)
#   を確かめ、通ったものだけ
#     ② レコード無しで EOF → 終了する
#     ③ streamhdr + C_OP(実在の op) を送ってから EOF → 終了する
#   を判定する。① が崩れたモジュールは **判定を出さずに FAIL** にする。
#
# ★★ 「終了した」だけでは足りない — **キャッシュを 1 つも作っていないこと**まで見る (ひさ指摘)
#   出力キャッシュのパスは **C_ARG_END で初めて渡る** (ptsGenericAgent.cpp:211 の outCache)。
#   つまり C_ARG_END 前に畳まれた agent は **書く先を知らない**はずで、1 バイトでも書いていたら
#   その不変条件が壊れている。壊れたまま撤収経路に乗ると、a4877e7 (#3591) が塞いだ
#   「完成済みのキャッシュを O_TRUNC で先頭から書き直す」がそのまま復活する。
#   ⇒ ②③ は **空の作業 dir と空のキャッシュ dir** で走らせ、終了後も**空のまま**であることを見る。
#
# ⚠ ③ の C_OP は **実在の op 名**を `--module-info` から採る。存在しない名前だと agent は
#   "unknown op" で ERROR 経路に入って終了するので、**EOF のおかげで終わったのか
#   エラーで終わったのか区別が付かない** (テストが常に緑になる)。
SRAVA="${1:?srava binary not given}"
AGENT="${2:?srava_agent binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

# ⚠ 実行体は **絶対パスに直す**。②③ は空の作業 dir へ cd してから走らせるので、相対パスのままだと
#   そこから見えなくなる (rc=127)。ctest からは $<TARGET_FILE:...> で絶対パスが来るが、手で
#   `sh test/... build-all/srava ...` と打つと相対で入る ⇒ 手打ちと ctest で振る舞いが変わる。
#   ⚠⚠ Windows の絶対パスは **ドライブ文字で始まる** (C:/... / C:\...) ので、`/*` だけ見ると
#     絶対パスに $(pwd) を前置してしまう。MinGW で実際に踏んだ:
#       EOF_FAIL: srava が実行できない: C:/…/build-f17/C:/…/build-f17/srava.exe
#     ⇒ ドライブ文字と UNC (//host/share) も絶対として扱う。
abspath() {
	case "$1" in
		/*|[A-Za-z]:/*|[A-Za-z]:\\*) printf %s "$1" ;;
		*)                             printf %s "$(pwd)/$1" ;;
	esac
}
SRAVA=$(abspath "$SRAVA")
AGENT=$(abspath "$AGENT")
[ -x "$SRAVA" ] || { echo "EOF_FAIL: srava が実行できない: $SRAVA"; exit 1; }
[ -x "$AGENT" ] || { echo "EOF_FAIL: srava_agent が実行できない: $AGENT"; exit 1; }

NG=0
WORK="$D/agent-eof"
rm -rf "$WORK"; mkdir -p "$WORK" || { echo "EOF_FAIL: mkdir $WORK"; exit 1; }

T_ALIVE=2          # ① 較正: ここまで生きていれば「待っている」と見なす
HOLD_ALIVE=3       # ① 較正: パイプを開けたままにする秒数。
                   #   ⚠ **T_ALIVE より少しだけ長く**する。シェルはパイプライン全体の終了を待つので、
                   #     ここを大きくすると timeout が agent を殺した後も書き手の sleep を待ち続け、
                   #     1 モジュールあたりその秒数を丸ごと払う (30 にしていて 22 本 x 30 秒で溢れた)。
T_EXIT=12          # ②③ EOF 後にこの秒数以内で終わること

# ---- この検定が走れる前提を先に確かめる (走れないなら **77 = Skipped**) ----
#   ★ 「ここでは確かめられない」の表し方を **77 に 1 つへ揃える** (ひさ判断 2026-09-25)。
#     ⚠⚠ PASS_REGULAR_EXPRESSION に skip の印を足す手は採らない — **飛ばした検定が緑と
#       同じ顔になる**。逆に何もしないと「Required regular expression not found」で**偽の赤**。
#       ⇒ CMakeLists 側に SKIP_RETURN_CODE 77 を置き、ここは 77 で抜ける。

# ① Windows (MinGW / MSYS) — **経路が塞がっている** (#3600)
#   agent は shell が作ったパイプから stdin を読めない。tinyState windows が wine で切り分けた:
#     CreateThreadpoolIo(fd) -> FAILED (Win32 87)  ⇒ ts2IOdescriptor::read() が -1 を返して終わり
#     (ReadFile / pump / complete_fill は 0 回。データは 1 バイトも読まれない)
#   陽性対照 = 同じ exe ・ 同じ wine で **ts2System が作った** (FILE_FLAG_OVERLAPPED) パイプなら
#     COUNT=4096 が届く ⇒ 壊れているのは wine でも exe でもなく **パイプの作り手**。
#   ⇒ ① の較正 (「EOF を与えなければ生きている」) が原理的に立たないので、判定を出さない。
#   ⚠ Cygwin は対象外 (CYGWIN_NT-*)。arch は posix + posix_Cygwin で、上書きは tsSignalCore.cpp
#     だけ ⇒ fd + select 版 = Linux と同一ソースなので塞がらない (実機で確認する)。
case "$(uname -s 2> /dev/null)" in
	MINGW*|MSYS*)
		echo "EOF_SKIP: Windows (MinGW) は **shell のパイプ経由の親→子 stdin が未対応** (#3600)"
		echo "  agent が stdin を読めないので、① の較正 (EOF を与えなければ生きている) が立たない。"
		echo "  ⇒ この検定は **飛ばした** (緑ではない)。Linux / macOS では実走して確かめている。"
		echo "  機序: CreateThreadpoolIo が同期ハンドルを受け付けない (Win32 87) ⇒ read() が -1。"
		echo "        ts2System が作った overlapped パイプなら届く (陽性対照 COUNT=4096)。"
		exit 77 ;;
esac

# ② 時間で打ち切る道具 — **前提として要求する**
#   ⚠⚠ 以前は道具が無ければシェルで番犬を立てていたが、**撤去した** (ひさ判断 2026-09-25)。
#     検定を回す 4 機すべてに coreutils が在る (Linux / MSYS / Cygwin は timeout ・ mac は gtimeout)
#     ⇒ 番犬は **どの機でも一度も走らない経路**だった。そこに fd の欠陥が潜んでいた:
#       素朴な `"$@" &` は **背景ジョブの stdin が /dev/null になる** (POSIX) ので agent が即 EOF を
#       見て rc=0 で終わり、① の較正が全数で崩れる (道具を外した PATH で 22 本すべて EOF_FAIL)。
#       直しは較正用の抜け道側にだけ入っていて、**合成した PATH を作るまで誰も踏めなかった**。
#     ⇒ **通らない経路を持つと、そこの欠陥には誰も気づけない。**経路は 1 本にする。
if   command -v timeout  > /dev/null 2>&1 ; then _TMO=timeout
elif command -v gtimeout > /dev/null 2>&1 ; then _TMO=gtimeout
else
	echo "EOF_SKIP: 時間で打ち切る道具 (timeout / gtimeout) が無いのでこの検定は走れない"
	echo "  この検定は「EOF を与えなければ agent が生きている」を先に確かめる (較正) ので、"
	echo "  一定時間で打ち切る手段が要る。無いと較正が立たず、判定を出せない。"
	echo "  ⇒ **coreutils を入れてください** (macOS: brew install coreutils / Debian: apt install coreutils)"
	exit 77
fi
_limited() {   # _limited <秒> <コマンド...> → 0 系 = 自分で終わった / **124 = 時間切れ**
	#  ★ 戻り値は timeout(1) の流儀 = 時間切れで **124**。判定側はこれを見る。
	"$_TMO" "$@"
}

# ---- pigwire の streamhdr (20 byte) と C_OP レコード (8 byte + op 名) ----
#   streamhdr = magic4 'P','W','I','G' / ver2 = 2 / endian1 = 1(LE) / hflags1 = 0 / pid4 / start8
#   rechdr    = len4 type2 rflags2                (すべて明示 LE。src/h/pig/c++/pigwire.h)
#   C_OP = 1 ・ payload は **op 名の生バイト** (wpBigParent.cpp:169 と同じ形)
STREAMHDR='\120\127\111\107\002\000\001\000\001\000\000\000\000\000\000\000\000\000\000\000'
c_op_record() {   # c_op_record <op 名> → 8 byte の rechdr を printf 用のエスケープで返す
	len=$(printf %s "$1" | wc -c | tr -d ' ')
	printf '\\%03o\\%03o\\%03o\\%03o\\001\\000\\000\\000' \
	       $((len % 256)) $(((len / 256) % 256)) $(((len / 65536) % 256)) $((len / 16777216))
}

# ---- 探索路に載っているモジュール (name と path) ----
#   材料は `srava --modules` の loaded 節 = 実際に dlopen できたものだけ。
#   組込 "pig" は path が "(組込)" なので自然に外れる。
modules() {
	"$SRAVA" --modules 2>&1 | awk '
		/^loaded:/        { f = 1; next }
		f && $1 == "name" { next }
		f && NF >= 5 && $NF ~ /\.(so|dll|dylib)$/ { print $1, $NF }'
}

# ---- 各モジュールが申告している op を 1 つずつ (③ 用) ----
#   ★ `--module-info` は **引数なしで全モジュール**を吐くので 1 回で済ませる
#     (モジュールごとに呼ぶと srava の起動が 22 回増える)。
OPMAP="$WORK/first_op.txt"
"$SRAVA" --module-info 2>&1 | awk '
	/^[a-z_0-9]+ +\(abi=/ { cur = $1; done_[cur] = 0; inops = 0; next }
	/^ *ops \(/           { inops = 1; next }
	inops && cur != "" && $1 ~ /^[a-z_]/ && $2 ~ /^nin=/ && !done_[cur] {
		print cur, $1; done_[cur] = 1
	}' > "$OPMAP"
first_op() { awk -v m="$1" '$1 == m { print $2; exit }' "$OPMAP"; }

# ---- 1 回走らせて rc を返す ----
#   $1 = .so / $2 = パイプへ流すバイト列 (printf のエスケープ・空可) / $3 = 開けたままにする秒数
#   $4 = timeout 秒 / $5 = ログの接尾辞
#   ★ 毎回 **空の cwd と空のキャッシュ dir** を用意して走らせる。終了後の中身は
#     `made_$_tag` に件数で残す (呼び手が判定する)。
run_agent() {
	_so="$1"; _bytes="$2"; _hold="$3"; _t="$4"; _tag="$5"
	_cwd="$WORK/run-$_tag/cwd"; _cache="$WORK/run-$_tag/cache"
	rm -rf "$WORK/run-$_tag"; mkdir -p "$_cwd" "$_cache"
	#  ⚠ env の前置き (VAR=v cmd) は **シェル関数には使えない** ので、subshell の中で export する。
	( cd "$_cwd" || exit 125
	  export SRAVA_CACHE_DIR="$_cache"
	  { [ -n "$_bytes" ] && printf "$_bytes"; sleep "$_hold"; } 2>/dev/null \
	    | _limited "$_t" "$AGENT" "$_so" > "$WORK/$_tag.out" 2> "$WORK/$_tag.err" )
	_rc=$?
	echo "$(count_entries "$_cwd") $(count_entries "$_cache")" > "$WORK/made-$_tag"
	echo $_rc
}
made() { awk '{print $1 + $2}' "$WORK/made-$1" 2>/dev/null; }
made_detail() { awk '{printf "cwd %s / cache %s", $1, $2}' "$WORK/made-$1" 2>/dev/null; }

# ---- dir の中の実体の数 (キャッシュを作っていないことの判定に使う) ----
count_entries() { find "$1" -mindepth 1 2>/dev/null | wc -l | tr -d ' '; }

# ★★ 計数の較正 — 「0 件でした」は **1 件を数えられる**ことを示して初めて主張になる。
#   ここを置かないと、find が使えない/パスが間違っている環境で **常に 0 = 常に緑**になる。
CAL="$WORK/_countcal"
mkdir -p "$CAL/sub" && : > "$CAL/f1" && : > "$CAL/sub/f2"
if [ "$(count_entries "$CAL")" != "3" ]; then
	echo "EOF_FAIL: ★計数の較正が崩れた — 3 件置いたのに $(count_entries "$CAL") 件と数えた"
	echo "          (この道具では「キャッシュを作っていない」を判定できない)"
	exit 1
fi
rm -rf "$CAL"
mkdir -p "$CAL"
if [ "$(count_entries "$CAL")" != "0" ]; then
	echo "EOF_FAIL: ★計数の較正が崩れた — 空 dir を $(count_entries "$CAL") 件と数えた"; exit 1
fi
rm -rf "$CAL"

n_mod=0; n_ok=0
echo "--- agent は C_ARG_END 前に stdin が切れたら終了するか / キャッシュを作らないか ---"

MODLIST="$WORK/modules.txt"
modules > "$MODLIST"
if [ ! -s "$MODLIST" ]; then
	echo "EOF_FAIL: モジュールが 1 つも列挙できない (材料が無いので判定できない)"
	exit 1
fi

while read -r name so; do
	[ -n "$name" ] || continue
	n_mod=$((n_mod + 1))

	# ① 較正 (負の対照) — EOF を与えなければ切られるまで生きていること
	rc=$(run_agent "$so" "" "$HOLD_ALIVE" "$T_ALIVE" "cal-$name")
	if [ "$rc" != "124" ]; then
		echo "EOF_FAIL: $name  ★較正が崩れた — EOF を与えていないのに rc=$rc で終わった"
		echo "          (= agent がそもそも待っていない。②③ の「終了した」は EOF の証拠にならない)"
		sed -n '1,4p' "$WORK/cal-$name.err" 2>/dev/null | sed 's/^/            /'
		NG=1
		continue
	fi

	# ② レコードを 1 つも送らずに EOF
	rc=$(run_agent "$so" "" 0 "$T_EXIT" "a-$name")
	if [ "$rc" = "124" ]; then
		echo "EOF_FAIL: $name  ② レコード無しで EOF → **${T_EXIT}s 以内に終わらない**"
		NG=1; continue
	fi
	if [ "$(made "a-$name")" != "0" ]; then
		echo "EOF_FAIL: $name  ② EOF で畳まれたのに **何かを書いている** ($(made_detail "a-$name"))"
		find "$WORK/run-a-$name" -mindepth 1 2>/dev/null | head -5 | sed 's/^/            /'
		NG=1; continue
	fi

	# ③ streamhdr + C_OP(実在 op) を送ってから EOF (C_ARG_END は送らない)
	op=$(first_op "$name")
	if [ -z "$op" ]; then
		echo "  ok   $name  (② のみ rc=$rc ・ ③ は op を申告していないので省略)"
		n_ok=$((n_ok + 1)); continue
	fi
	bytes="$STREAMHDR$(c_op_record "$op")$(printf %s "$op" | sed 's/./\\x&/g')"
	rc3=$(run_agent "$so" "$bytes" 0 "$T_EXIT" "b-$name")
	if [ "$rc3" = "124" ]; then
		echo "EOF_FAIL: $name  ③ C_OP($op) 送信後に EOF → **${T_EXIT}s 以内に終わらない**"
		NG=1; continue
	fi
	if [ "$(made "b-$name")" != "0" ]; then
		echo "EOF_FAIL: $name  ③ C_OP($op) の後 EOF で畳まれたのに **何かを書いている** ($(made_detail "b-$name"))"
		find "$WORK/run-b-$name" -mindepth 1 2>/dev/null | head -5 | sed 's/^/            /'
		NG=1; continue
	fi
	echo "  ok   $name  (② rc=$rc ・ ③ op=$op rc=$rc3 ・ 作った実体 0 ・ 較正 rc=124)"
	n_ok=$((n_ok + 1))
done < "$MODLIST"

n_mod=$(wc -l < "$MODLIST" | tr -d ' ')
echo "--- 対象 $n_mod モジュール ---"
[ "$NG" = "0" ] && echo "AGENT_EOF_OK" || echo "AGENT_EOF_NG"
exit $NG
