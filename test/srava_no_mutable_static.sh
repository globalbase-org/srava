#!/bin/sh
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
# モジュール実装に **可変なプロセス大域変数 (static)** が増えていないかの lint。$1 = ソース根。
#
# ★なぜ要るか (ひさ指示 2026-08-26):
#   モジュールは **in-proc (EXEC_THREAD) で走りうる** — 実際 manifold は既定が in-proc、
#   openvdb 系は module(so,{exec_default:"thread"}) で in-proc にできる。その場合
#   **1 プロセスに複数の op が同居**するので、モジュール実装が可変な static を持つと
#   op どうしで混線する (直前の op の値を次の op が読む)。
#   実例: 例外の理由をモジュール大域の static に溜める書き方をして指摘された。
#   → 理由は **呼び手のバッファ**へ書く形に直した (ggMesh / ocShape の *_guard)。
#
# ★ 何を見ているか: `static <型> <名前> ... ;` の形 = **可変なファイルスコープ/関数内 static 変数**。
#   ★ **クラスの static メンバ変数も対象**にする (ひさ指示)。置き場所がクラスの中でも
#     プロセスに 1 つであることは変わらないので、混線の性質は同じ。
#   ⚠ 以下だけ対象外 (正当な用途):
#     - `static` **関数** (メンバ関数含む)。このリポジトリは戻り型を独立行に書くので、
#       1 行で書かれた場合は '(' や '{' を含む行として除ける
#     - `static const` / `static constexpr` (読み取り専用なので混線しない)
#
# ★ 正当な理由があるものは test/mutable_static_allow.txt に **理由付きで**登録する。
#   「消せない static」ではなく「**なぜプロセスに 1 つでよいか**」を書くこと。
ROOT="${1:-.}"
ALLOW="$ROOT/test/mutable_static_allow.txt"
[ -f "$ALLOW" ] || { echo "FAIL: 許可リストが無い: $ALLOW"; exit 1; }
rm -f "$ROOT/.mutable_static_new"   # ⚠ 前回中断の残骸で誤検出しないよう毎回消す

# 検出: 行頭 static で始まり ';' を持つ行のうち、関数定義/宣言 ('(' '{') でないもの。
# ⚠ **コメントを先に剥がす**: 末尾コメントに '(' が入っているだけで関数と誤判定して
#   取りこぼす (実際 geogram の g_geoInit を偽陰性で見逃した)。
# ⚠⚠ **-P (PCRE) を使わない**。BSD grep (macOS の既定) に -P は無く、
#   `grep: invalid option -- P` を出して **何も検出しないまま素通り**する。
#   ★★ 2026-09-16 に実際に踏んだ: この本は **mac で長らく緑だったが、何も測っていなかった**。
#     bench (Linux) が #3525 の `static char msgbuf[]` を捕まえて初めて分かった。
#     ⇒ 検出は -E (基本の拡張正規表現) で書き、除外は後段の grep -v で行う。
#   ⇒ 下の「較正」も参照 — **測れることを先に確かめてから 0 を報告する**。
scan() {   # scan <根> → 「file:line:本文」を出す
	( cd "$1" && grep -rnE '^[[:space:]]*static[[:space:]]+[^;]*;' \
	    modules/*/c++/*.cpp modules/*/c++/*.h modules/*/*.cpp modules/*/h/*/c++/*.h 2>/dev/null ) \
	| sed 's|/\*.*||; s|//.*||' \
	| grep -E ';' \
	| grep -vE '[({]' \
	| grep -vE ':[[:space:]]*static[[:space:]]+(const|constexpr|inline|template)[[:space:]]'
}

# 変数名の取り出し (= や ; の直前の識別子)。許可リストの照合に使う。
# ⚠⚠ **\s と \? は GNU sed 拡張**で BSD sed (macOS の既定) には無い。使うと *黙って
#   一致しなくなり* 名前が '?' になる ⇒ 許可リストの照合 (`^file name`) が **絶対に当たらない**
#   = 登録しても無視され続ける。⇒ POSIX の [[:space:]] と \{0,1\} で書く。
# ★ 配列 (`static char buf[192];`) も拾えるよう、識別子の後ろの [..] を任意で許す。
extract_name() {
	echo "$1" | sed -n 's/.*static[^;]*[ *]\([A-Za-z_][A-Za-z0-9_]*\)\(\[[^;]*\]\)\{0,1\}[[:space:]]*\(=[^;]*\)\{0,1\};.*/\1/p'
}

# ---- ★★ 較正: **測れることを先に確かめてから 0 を報告する** ----
#   ⚠ 「0 件でした」は *何も見せない* ので、**壊れた検出器と見分けがつかない**。
#     ★ これが無かったために mac では -P 非対応が 0 件に化けて気づけなかった (2026-09-16)。
#   ★★ **両側を植える** — *拾うべきもの* と *拾ってはいけないもの* の両方。
#     片側だけだと「当たった / 当たらない」の意味が半分になる。
#     ⚠ そして期待値は **仕様から書く** (実装の鏡にしない)。実装を写すと欠陥を追認する。
#       ⇒ 下の表の根拠はこのファイル冒頭の「何を見ているか」であって、scan() の中身ではない。
#
#   仕様 (冒頭より): 見るのは「可変なファイルスコープ/関数内 static 変数」。
#     ★ クラスの static メンバ変数も対象 (置き場所が変わってもプロセスに 1 つは同じ)
#     ⚠ 対象外: static 関数 ・ static const / constexpr (読み取り専用なので混線しない)
CANARY="$ROOT/.mutable_static_canary"
rm -rf "$CANARY"
mkdir -p "$CANARY/modules/canary/c++" || { echo "FAIL: 較正用の一時ディレクトリを作れない"; exit 1; }
cat > "$CANARY/modules/canary/c++/canary.cpp" <<'CANARY_EOF'
static char must_hit_array[8];
static int must_hit_plain;
static int must_hit_init = 3;
static unsigned long must_hit_qualified;
	static int must_hit_fnlocal;
static const int no_hit_const = 1;
static constexpr int no_hit_constexpr = 2;
static int no_hit_fn_decl(int a);
static inline int no_hit_inline_fn(int a) { return a; }
CANARY_EOF
CAL=$(scan "$CANARY")
rm -rf "$CANARY"
# ★ 拾うべきものが全部拾えているか (1 つでも欠けたら検出器は信用できない)
for _w in must_hit_array must_hit_plain must_hit_init must_hit_qualified must_hit_fnlocal ; do
	case "$CAL" in
	*"$_w"*) ;;
	*)	echo "FAIL: 検出器が **$_w** を見つけられない = この検査は測れていない"
		echo "      (BSD grep で -P を使っている / パターンが壊れている等)。得られた出力:"
		printf '%s\n' "$CAL" | sed 's/^/      /'
		exit 1 ;;
	esac
done
# ⚠ 拾ってはいけないものを拾っていないか (拾うと *正当なコードが赤くなる* ので実害が出る)
for _n in no_hit_const no_hit_constexpr no_hit_fn_decl no_hit_inline_fn ; do
	case "$CAL" in
	*"$_n"*)
		echo "FAIL: 検出器が **$_n** を拾っている = 仕様外のものを赤くしている"
		printf '%s\n' "$CAL" | sed 's/^/      /'
		exit 1 ;;
	esac
done
# ★★ 名前の取り出しも較正する — 許可リストの照合は **名前で** 行うので、'?' になると
#   *登録しても一生無視される* (2026-09-16 に BSD sed の \s / \? で実際にそうなっていた)。
#   ⚠⚠ 上の must_hit の検査では **これを検定できない** — CAL には元の行がそのまま入っており、
#     名前は「行の中に在る」だけで、*取り出せているか* は別の話。⇒ 取り出しを実際に走らせる。
#     ★ 私は最初この違いを見落として、当たらない検査を書いた (書いてすぐ気づいて直した)。
echo "$CAL" | while IFS= read -r _L ; do
	[ -n "$_L" ] || continue
	_N=$(extract_name "$_L")
	case "$_N" in
	must_hit_*) ;;
	*)	echo "FAIL: 変数名を取り出せていない (得た名前 = '$_N') = 許可リストが機能しない"
		echo "      行: $_L"
		exit 1 ;;
	esac
done || exit 1

HITS=$(scan "$ROOT")

NEW=0
echo "$HITS" | while IFS= read -r L; do
	[ -n "$L" ] || continue
	F=$(echo "$L" | cut -d: -f1)
	# 変数名 = 最後の識別子 (= や ; の直前)
	# ⚠⚠ **\s と \? は GNU sed 拡張**で BSD sed (macOS の既定) には無い。使うと *黙って
	#   一致しなくなり* 名前が '?' になる ⇒ 許可リストの照合 (`^file name`) が **絶対に当たらない**
	#   = 登録しても無視され続ける。⇒ POSIX の [[:space:]] と \{0,1\} で書く。
	#   ★ 配列 (`static char buf[192];`) も拾えるよう、識別子の後ろの [..] を任意で許す。
	#   ⚠ この行は上の -P と **同じ家系の移植性の穴**だった (2026-09-16 に両方直した)。
	N=$(extract_name "$L")
	[ -n "$N" ] || N="?"
	if grep -q "^$F[[:space:]]\+$N\([[:space:]]\|$\)" "$ALLOW" 2>/dev/null; then
		continue
	fi
	echo "FAIL: 許可リストに無い可変 static: $F の '$N'"
	echo "      $L"
	echo "$F $N" >> "$ROOT/.mutable_static_new"
done

if [ -f "$ROOT/.mutable_static_new" ]; then
	NEW=$(wc -l < "$ROOT/.mutable_static_new")
	rm -f "$ROOT/.mutable_static_new"
	echo ""
	echo "  ★ モジュールは in-proc (EXEC_THREAD) で走りうるので、1 プロセスに複数 op が同居する。"
	echo "    可変な static はその op どうしで混線する。理由は呼び手のバッファへ渡すなど、"
	echo "    **リエントラント**に書くこと。プロセスに 1 つでよい正当な理由があるなら、"
	echo "    その理由を添えて $ALLOW に登録する。"
	exit 1
fi

N=$(echo "$HITS" | grep -c . )
echo "NO-MUTABLE-STATIC-OK 検出 $N 件はすべて許可リスト済み (検出器は較正済み)"
