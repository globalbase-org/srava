#!/bin/sh
# ★ #3569: 引数無し起動 (warm) が使う **既定ソースは自分で module を読む** ので、この検定で
#   モジュールを要るのは最後の in-code ケースだけ。そこに cgal を明示した (all.sra は外した)。
# SRAVA_CACHE_RETAIN の回帰テスト: 終了時クリーンアップの保持方針が切り替わること。
#   warm で used キャッシュを作り、その複製を未使用 hash(ffff…)= stray(別プログラムの残骸を模す)として置く。
#   stray の扱いが方針で変わることを確認:
#     RETAIN=all       → 残る(完了は消さない)
#     RETAIN=過去日     → 残る(stray の mtime=今 はその日より新しい)
#     RETAIN=未来日     → 消える(今 はその日より古い扱い → 全未使用が cutoff 以前)
#     既定(未設定)      → 消える(即削除)
#   used キャッシュはどのモードでも残る。touch を使わず日付 cutoff で新旧を作るので移植性がある。
# 引数 $1 = srava 実行体パス。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
set -e
SRAVA="$1"
# ★ #3569 の続き: 既定ソース (引数無し起動) の 1 行目は `include "module/all.sra"` で、
#   探索順は ① include 元の dir ② $SRAVA_PATH ③ install prefix。
#   ②③ とも無い機械では warm() の素の起動が落ち、`>/dev/null 2>&1` と `set -e` のせいで
#   **出力ゼロで即死**する (Linux は ③ = /usr/local/share/srava/lib が在るので露見しない)。
#   ⇒ install 済みかどうかに依存しないよう、ソース木の lib を $SRAVA_PATH に据える。
SRAVA_PATH="$(cd "$(dirname "$0")/../lib" && pwd)"
export SRAVA_PATH
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
STRAY="$D/ffffffffffffffff.cache"

warm() {                                       # used キャッシュを作り stray を(再)設置
	"$SRAVA" >/dev/null 2>&1
	one=$(ls "$D"/*.cache | grep -v ffffffffffffffff | head -1)
	cp "$one" "$STRAY"
}
fail() { echo "FAIL: $1"; exit 1; }

rm -rf "$D"; warm
SRAVA_CACHE_RETAIN=all "$SRAVA" >/dev/null 2>&1
[ -e "$STRAY" ] || fail "RETAIN=all: stray was removed"
[ -e "$one" ]   || fail "RETAIN=all: used cache was removed"

rm -rf "$D"; warm
SRAVA_CACHE_RETAIN=2000-01-01 "$SRAVA" >/dev/null 2>&1
[ -e "$STRAY" ] || fail "RETAIN=past-date: recent stray was removed"

rm -rf "$D"; warm
SRAVA_CACHE_RETAIN=2099-01-01 "$SRAVA" >/dev/null 2>&1
[ ! -e "$STRAY" ] || fail "RETAIN=future-date: old stray was not removed"
[ -e "$one" ]     || fail "RETAIN=future-date: used cache was removed"

rm -rf "$D"; warm
"$SRAVA" >/dev/null 2>&1
[ ! -e "$STRAY" ] || fail "default(immediate): stray was not removed"

# in-code 設定: env(SRAVA_CACHE_RETAIN)を未設定にしたまま、プログラムが CACHE_RETAIN="all" を
# 代入 → CACHE_DIR と同様コードから方針を上書きできる(env より優先)。stray は残るはず。
# 出力シンク(export)を付けて終了コードを決定的(0)にする(結果未使用だと teardown で非0 になり得る)。
rm -rf "$D"; warm
SRAVA_SOURCE='module("cgal.so",{}); CACHE_RETAIN = "all"; export("/tmp/srava_retain_incode.stl", box(2,2,2) ||| box(1,1,1));' "$SRAVA" >/dev/null 2>&1
[ -e "$STRAY" ] || fail "in-code CACHE_RETAIN=all: stray was removed"

echo "RETAIN_OK"
