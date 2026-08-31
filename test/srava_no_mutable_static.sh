#!/bin/sh
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
HITS=$(cd "$ROOT" && grep -rnP '^\s*static\s+(?!const\b|constexpr\b|inline\b|template\b)[^;]*;' \
        modules/*/c++/*.cpp modules/*/c++/*.h modules/*/*.cpp modules/*/h/*/c++/*.h 2>/dev/null \
      | sed 's|/\*.*||; s|//.*||' \
      | grep -P ';' \
      | grep -vP '[({]')

NEW=0
echo "$HITS" | while IFS= read -r L; do
	[ -n "$L" ] || continue
	F=$(echo "$L" | cut -d: -f1)
	# 変数名 = 最後の識別子 (= や ; の直前)
	N=$(echo "$L" | sed -n 's/.*static[^;]*[ *]\([A-Za-z_][A-Za-z0-9_]*\)\s*\(=.*\)\?;.*/\1/p')
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
echo "NO-MUTABLE-STATIC-OK 検出 $N 件はすべて許可リスト済み"
